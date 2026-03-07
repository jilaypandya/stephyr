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

/* -------------------------------------------------------------------------
 * Shared helpers
 * -------------------------------------------------------------------------
 */

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

/* Discard every byte currently in the UART emulator RX FIFO. */
static void uart_drain(void)
{
	unsigned char c;

	while (uart_poll_in(uart_dev, &c) == 0) {
	}
}

/* Send a frame and sleep long enough for the subsystem thread to process it. */
static void send_frame(const struct can_frame *f)
{
	int err = can_send(can_dev, f, K_MSEC(100), NULL, NULL);

	zassert_ok(err, "can_send id=0x%03x failed (err %d)", f->id, err);
	k_sleep(K_MSEC(500));
}

/* Send a zero-DLC frame with the given CAN ID (used for trigger frames). */
static void send_trigger(uint32_t id)
{
	const struct can_frame trigger = {
		.flags = 0,
		.id    = id,
		.dlc   = 0,
	};

	send_frame(&trigger);
}

/* Common hardware bring-up used by both suites. */
static void hw_setup(void)
{
	int err;

	zassert_true(device_is_ready(can_dev),  "CAN device not ready");
	zassert_true(device_is_ready(uart_dev), "UART device not ready");

	(void)can_stop(can_dev);
	err = can_set_mode(can_dev, CAN_MODE_LOOPBACK);
	zassert_ok(err, "failed to set CAN loopback mode (err %d)", err);
	err = can_start(can_dev);
	zassert_ok(err, "failed to restart CAN controller (err %d)", err);
}

/* =========================================================================
 * Suite: can_uart_replay  (scenario: can_uart_replay.default)
 *
 * No start/stop triggers — printing begins immediately and runs forever.
 * =========================================================================
 */

static void *default_setup(void *f)
{
	ARG_UNUSED(f);
	hw_setup();
	return NULL;
}

static void default_before(void *f)
{
	ARG_UNUSED(f);
	uart_drain();
}

ZTEST_SUITE(can_uart_replay, NULL, default_setup, default_before, NULL, NULL);

/* Smoke-test: 10 frames are sent without error. */
ZTEST(can_uart_replay, test_send)
{
	for (int i = 0; i < 10; i++) {
		int err = can_send(can_dev, &test_frame, K_MSEC(100), NULL, NULL);

		zassert_ok(err, "failed to send CAN frame %d (err %d)", i, err);
	}
}

/* A sent frame must appear on the UART immediately (no trigger required). */
ZTEST(can_uart_replay, test_uart_output)
{
	if (CONFIG_CAN_UART_REPLAY_START_ID != 0xFFFF) {
		ztest_test_skip();
	}

	char expected[80];
	char line[80];

	snprintf(expected, sizeof(expected),
		 "RX id=0x%03x dlc=%u data=%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
		 test_frame.id, test_frame.dlc,
		 test_frame.data[0], test_frame.data[1],
		 test_frame.data[2], test_frame.data[3],
		 test_frame.data[4], test_frame.data[5],
		 test_frame.data[6], test_frame.data[7]);

	send_frame(&test_frame);

	int len = uart_read_line(line, sizeof(line), K_MSEC(200));

	zassert_true(len > 0, "no UART output received");
	zassert_not_null(strstr(line, expected),
			 "UART output mismatch\n  got:      '%s'\n  want suffix: '%s'",
			 line, expected);
}

/* =========================================================================
 * Suite: can_uart_replay_triggers  (scenario: can_uart_replay.start_stop)
 *
 * Printing is gated by a start-trigger ID (0x100) and latched off by a
 * stop-trigger ID (0x200).
 * =========================================================================
 */

static void *triggers_setup(void *f)
{
	ARG_UNUSED(f);
	hw_setup();
	return NULL;
}

static void triggers_before(void *f)
{
	ARG_UNUSED(f);
	uart_drain();
}

ZTEST_SUITE(can_uart_replay_triggers, NULL, triggers_setup, triggers_before, NULL, NULL);

/*
 * test_no_output_before_start
 *
 * Frames received before the start-trigger ID arrive must not produce any
 * UART output.
 */
ZTEST(can_uart_replay_triggers, test_no_output_before_start)
{
	if (CONFIG_CAN_UART_REPLAY_START_ID == 0xFFFF) {
		ztest_test_skip();
	}

	char line[80];

	send_frame(&test_frame);

	int len = uart_read_line(line, sizeof(line), K_MSEC(100));

	zassert_equal(len, 0,
		      "expected silence before start trigger, got '%s'", line);
}

/*
 * test_output_after_start
 *
 * Frames received after the start-trigger ID must be printed on UART.
 */
ZTEST(can_uart_replay_triggers, test_output_after_start)
{
	if (CONFIG_CAN_UART_REPLAY_START_ID == 0xFFFF) {
		ztest_test_skip();
	}

	char expected[80];
	char line[80];

	snprintf(expected, sizeof(expected),
		 "RX id=0x%03x dlc=%u data=%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
		 test_frame.id, test_frame.dlc,
		 test_frame.data[0], test_frame.data[1],
		 test_frame.data[2], test_frame.data[3],
		 test_frame.data[4], test_frame.data[5],
		 test_frame.data[6], test_frame.data[7]);

	send_trigger(CONFIG_CAN_UART_REPLAY_START_ID);
	uart_drain(); /* discard any log line emitted for the trigger itself */

	send_frame(&test_frame);

	int len = uart_read_line(line, sizeof(line), K_MSEC(200));

	zassert_true(len > 0, "expected UART output after start trigger, got none");
	zassert_not_null(strstr(line, expected),
			 "UART output mismatch\n  got:      '%s'\n  want suffix: '%s'",
			 line, expected);
}

/*
 * test_no_output_after_stop
 *
 * Once the stop-trigger ID is received, all subsequent frames must be
 * suppressed permanently.
 */
ZTEST(can_uart_replay_triggers, test_no_output_after_stop)
{
	if (CONFIG_CAN_UART_REPLAY_START_ID == 0xFFFF ||
	    CONFIG_CAN_UART_REPLAY_STOP_ID  == 0xFFFF) {
		ztest_test_skip();
	}

	char line[80];

	/* Activate printing first. */
	send_trigger(CONFIG_CAN_UART_REPLAY_START_ID);
	uart_drain();

	/* Now latch it off. */
	send_trigger(CONFIG_CAN_UART_REPLAY_STOP_ID);
	uart_drain();

	/* Send multiple data frames — none should appear on UART. */
	for (int i = 0; i < 3; i++) {
		send_frame(&test_frame);
	}

	int len = uart_read_line(line, sizeof(line), K_MSEC(100));

	zassert_equal(len, 0,
		      "expected silence after stop trigger, got '%s'", line);
}
