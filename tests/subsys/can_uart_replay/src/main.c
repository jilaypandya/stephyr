/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/can.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <string.h>
#include <stdio.h>
#include <utils/format_can.h>

#define TEST_CAN_ID  0x123
#define TEST_CAN_DLC 8

struct can_uart_replay_fixture {
	const struct device *can_dev;
	const struct device *uart_dev;
	const struct can_frame test_frame;
};

/* -------------------------------------------------------------------------
 * Shared helpers
 * -------------------------------------------------------------------------
 */

/* Read up to buf_len-1 chars from uart_dev until '\n' or timeout. */
static int uart_read_line(const struct device *uart_dev, char *buf, size_t buf_len,
			  k_timeout_t timeout)
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
static void uart_drain(const struct device *uart_dev)
{
	unsigned char c;

	while (uart_poll_in(uart_dev, &c) == 0) {
	}
}

/* =========================================================================
 * Suite: can_uart_replay  (scenario: can_uart_replay.default)
 *
 * No start/stop triggers — printing begins immediately and runs forever.
 * =========================================================================
 */

static void *can_uart_replay_setup(void)
{
	static struct can_uart_replay_fixture fixture = {
		.can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus)),
		.uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_uart_can)),
		.test_frame =
			{
				.flags = 0,
				.id = TEST_CAN_ID,
				.dlc = TEST_CAN_DLC,
				.data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
			},
	};
	int err;

	zassert_true(device_is_ready(fixture.can_dev), "CAN device not ready");
	zassert_true(device_is_ready(fixture.uart_dev), "UART device not ready");

	(void)can_stop(fixture.can_dev);
	err = can_set_mode(fixture.can_dev, CAN_MODE_LOOPBACK);
	zassert_ok(err, "failed to set CAN loopback mode (err %d)", err);
	err = can_start(fixture.can_dev);
	zassert_ok(err, "failed to restart CAN controller (err %d)", err);

	return &fixture;
}

static void can_uart_replay_before(void *f)
{
	struct can_uart_replay_fixture *fixture = f;
	uart_drain(fixture->uart_dev);
}

ZTEST_SUITE(can_uart_replay, NULL, can_uart_replay_setup, can_uart_replay_before, NULL, NULL);

/* A sent frame must appear on the UART immediately (no trigger required). */
ZTEST_F(can_uart_replay, test_uart_output)
{
	if (CONFIG_CAN_UART_REPLAY_START_ID != 0xFFFF) {
		ztest_test_skip();
	}

	char expected[80];
	char line[80];

	format_can_frame_body(expected, sizeof(expected), &fixture->test_frame);

	can_send(fixture->can_dev, &fixture->test_frame, K_MSEC(100), NULL, NULL);
	k_sleep(K_MSEC(50)); /* allow some time for the frame to be processed */
	int len = uart_read_line(fixture->uart_dev, line, sizeof(line), K_MSEC(200));

	zassert_true(len > 0, "no UART output received");
	zassert_not_null(strstr(line, expected),
			 "UART output mismatch\n  got:      '%s'\n  want suffix: '%s'", line,
			 expected);
}

ZTEST_F(can_uart_replay, test_hello_specialized_trigger)
{
	if (CONFIG_CAN_UART_REPLAY_HELLO_SPECIALIZED_ID == 0xFFFF) {
		ztest_test_skip();
	}

	char expected[] = "hello specialized";
	char line[80];

	struct can_frame hello_frame = {
		.id = CONFIG_CAN_UART_REPLAY_HELLO_SPECIALIZED_ID,
		.dlc = 0,
	};

	can_send(fixture->can_dev, &hello_frame, K_MSEC(100), NULL, NULL);
	k_sleep(K_MSEC(50)); /* allow some time for the frame to be processed */
	int len = uart_read_line(fixture->uart_dev, line, sizeof(line), K_MSEC(200));

	zassert_true(len > 0, "no UART output received");
	zassert_not_null(strstr(line, expected),
			 "UART output mismatch\n  got:      '%s'\n  want substring: '%s'", line,
			 expected);
}

/*
 * test_no_output_before_start
 *
 * Frames received before the start-trigger ID arrive must not produce any
 * UART output.
 */
ZTEST_F(can_uart_replay, test_no_output_before_start)
{
	if (CONFIG_CAN_UART_REPLAY_START_ID == 0xFFFF) {
		ztest_test_skip();
	}

	char line[80];

	can_send(fixture->can_dev, &fixture->test_frame, K_MSEC(100), NULL, NULL);

	int len = uart_read_line(fixture->uart_dev, line, sizeof(line), K_MSEC(100));

	zassert_equal(len, 0, "expected silence before start trigger, got '%s'", line);
}

/*
 * test_output_after_start
 *
 * Frames received after the start-trigger ID must be printed on UART.
 */
ZTEST_F(can_uart_replay, test_output_after_start)
{
	if (CONFIG_CAN_UART_REPLAY_START_ID == 0xFFFF) {
		ztest_test_skip();
	}

	char expected[80];
	char line[80];

	format_can_frame_body(expected, sizeof(expected), &fixture->test_frame);

	struct can_frame start_frame = {
		.id = CONFIG_CAN_UART_REPLAY_START_ID,
		.dlc = 0,
	};
	can_send(fixture->can_dev, &start_frame, K_MSEC(100), NULL, NULL);
	k_sleep(K_MSEC(50)); /* allow some time for the frame to be processed */
	can_send(fixture->can_dev, &fixture->test_frame, K_MSEC(100), NULL, NULL);
	k_sleep(K_MSEC(50)); /* allow some time for the frame to be processed */

	int len = uart_read_line(fixture->uart_dev, line, sizeof(line), K_MSEC(200));

	zassert_true(len > 0, "expected UART output after start trigger, got none");
	zassert_not_null(strstr(line, expected),
			 "UART output mismatch\n  received:      '%s'\n  expected: '%s'", line,
			 expected);
}

/*
 * test_no_output_after_stop
 *
 * Once the stop-trigger ID is received, all subsequent frames must be
 * suppressed permanently.
 */
ZTEST_F(can_uart_replay, test_no_output_after_stop)
{
	if (CONFIG_CAN_UART_REPLAY_START_ID == 0xFFFF || CONFIG_CAN_UART_REPLAY_STOP_ID == 0xFFFF) {
		ztest_test_skip();
	}

	char line[80];

	/* Activate printing first. */
	struct can_frame start_stop_frame;
	start_stop_frame.id = CONFIG_CAN_UART_REPLAY_START_ID;
	can_send(fixture->can_dev, &start_stop_frame, K_MSEC(100), NULL, NULL);
	k_sleep(K_MSEC(50)); /* allow some time for the frame to be processed */

	can_send(fixture->can_dev, &start_stop_frame, K_MSEC(100), NULL, NULL);
	k_sleep(K_MSEC(50)); /* allow some time for the frame to be processed */

	/* Send multiple data frames — none should appear on UART. */
	for (int i = 0; i < 3; i++) {
		can_send(fixture->can_dev, &fixture->test_frame, K_MSEC(100), NULL, NULL);
	}

	int len = uart_read_line(fixture->uart_dev, line, sizeof(line), K_MSEC(100));

	zassert_equal(len, 0, "expected silence after stop trigger, got '%s'", line);
}
