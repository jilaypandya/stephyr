/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/can.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <string.h>
#include <stdio.h>

#define TEST_CAN_ID  0x123
#define TEST_CAN_DLC 8

static const struct device *can_dev  = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_uart_can));

static const struct can_frame test_frame = {
	.flags = 0,
	.id    = TEST_CAN_ID,
	.dlc   = TEST_CAN_DLC,
	.data  = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
};

/* Read up to buf_len-1 chars from uart_dev until '\n' or timeout. */
static int uart_read_line(char *buf, size_t buf_len, k_timeout_t timeout)
{
	int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
	size_t pos = 0;
	unsigned char c;

	while (pos < buf_len - 1) {
		if (uart_poll_in(uart_dev, &c) == 0) {
			buf[pos++] = c;
			if (c == '\n') {
				break;
			}
		} else {
			if (k_uptime_get() >= deadline) {
				break;
			}
			k_yield();
		}
	}

	buf[pos] = '\0';
	return pos;
}

static void *can_uart_replay_setup(void)
{
	int err;

	zassert_true(device_is_ready(can_dev),  "CAN device not ready");
	zassert_true(device_is_ready(uart_dev), "UART device not ready");

	/* Stop the controller (started by the subsystem SYS_INIT),
	 * switch to loopback mode so TX frames are echoed to RX filters,
	 * then restart. */
	(void)can_stop(can_dev);

	err = can_set_mode(can_dev, CAN_MODE_LOOPBACK);
	zassert_ok(err, "failed to set CAN loopback mode (err %d)", err);

	err = can_start(can_dev);
	zassert_ok(err, "failed to restart CAN controller (err %d)", err);

	return NULL;
}

static void can_uart_replay_teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	(void)can_stop(can_dev);
}

ZTEST_SUITE(can_uart_replay, NULL,
	    can_uart_replay_setup,
	    NULL,
	    NULL,
	    can_uart_replay_teardown);

ZTEST(can_uart_replay, test_send)
{
	int err;

	for (int i = 0; i < 10; i++) {
		err = can_send(can_dev, &test_frame, K_MSEC(100), NULL, NULL);
		zassert_ok(err, "failed to send CAN frame %d (err %d)", i, err);
	}
}

ZTEST(can_uart_replay, test_uart_output)
{
	char expected[80];
	char line[80];
	int err;

	/* Build the exact line the subsystem formats */
	snprintf(expected, sizeof(expected),
		 "RX id=0x%03x dlc=%u data=%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
		 test_frame.id, test_frame.dlc,
		 test_frame.data[0], test_frame.data[1],
		 test_frame.data[2], test_frame.data[3],
		 test_frame.data[4], test_frame.data[5],
		 test_frame.data[6], test_frame.data[7]);

	err = can_send(can_dev, &test_frame, K_MSEC(100), NULL, NULL);
	zassert_ok(err, "failed to send CAN frame (err %d)", err);

	/* Give the subsystem thread time to dequeue and write to UART */
	k_sleep(K_MSEC(50));

	int len = uart_read_line(line, sizeof(line), K_MSEC(200));

	zassert_true(len > 0, "no UART output received");
	zassert_mem_equal(line, expected, strlen(expected),
			  "UART output mismatch\n  got:      '%s'\n  expected: '%s'",
			  line, expected);
}
