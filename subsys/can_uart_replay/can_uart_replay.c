/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <zephyr/sys/iterable_sections.h>
#include <stdio.h>
#include <string.h>
#include <utils/format_time.h>
#include <utils/format_can.h>

LOG_MODULE_REGISTER(can_uart_replay, CONFIG_CAN_UART_REPLAY_LOG_LEVEL);

#define CAN_RX_MSGQ_MAX_FRAMES 16

CAN_MSGQ_DEFINE(can_rx_msgq, CAN_RX_MSGQ_MAX_FRAMES);

/* RX filter — standard 11-bit IDs only; ID and mask controlled via Kconfig.
 * Trigger IDs (start/stop/hello) are ORed into the mask at init time so they
 * always pass the hardware filter regardless of the user-configured range.
 */
static struct can_filter can_rx_filter = {
	.id = CONFIG_CAN_UART_REPLAY_FILTER_ID,
	.mask = CONFIG_CAN_UART_REPLAY_FILTER_MASK,
	.flags = 0,
};

static const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_uart_can));

static void uart_write_str(const char *str)
{
	while (*str) {
		uart_poll_out(uart_dev, *str++);
	}
}

/* -------------------------------------------------------------------------
 * CAN ID handler table — iterable section
 * -------------------------------------------------------------------------
 * Each entry binds one CAN ID (0xFFFF = disabled) to a handler function.
 * The handler returns true if it consumed the frame (no further processing),
 * false to fall through to the normal RX print path.
 */

static bool printing_active = (CONFIG_CAN_UART_REPLAY_START_ID == 0xFFFF);

typedef bool (*can_id_handler_fn)(const struct can_frame *frame);

struct can_id_handler {
	uint32_t id;
	can_id_handler_fn fn;
};

/* Register entries in the iterable section. */
#define CAN_ID_HANDLER_DEFINE(name, _id, _fn)                                                      \
	STRUCT_SECTION_ITERABLE(can_id_handler, name) = {.id = (_id), .fn = (_fn)}

#if CONFIG_CAN_UART_REPLAY_HELLO_SPECIALIZED_ID != 0xFFFF
/* --- hello specialized -------------------------------------------------- */
static bool handle_hello_specialized(const struct can_frame *frame)
{
	char line[32];

	snprintf(line, sizeof(line), "hello specialized id=0x%03x\r\n", frame->id);
	LOG_INF("hello specialized");
	uart_write_str(line);
	return true;
}

CAN_ID_HANDLER_DEFINE(handler_hello, CONFIG_CAN_UART_REPLAY_HELLO_SPECIALIZED_ID,
		      handle_hello_specialized);
#endif

#if CONFIG_CAN_UART_REPLAY_START_ID != 0xFFFF
/* --- start trigger ------------------------------------------------------- */
static bool handle_start(const struct can_frame *frame)
{
	printing_active = true;
	LOG_INF("Start trigger received (id=0x%03x), printing enabled", frame->id);
	return true;
}

CAN_ID_HANDLER_DEFINE(handler_start, CONFIG_CAN_UART_REPLAY_START_ID, handle_start);
#endif

#if CONFIG_CAN_UART_REPLAY_STOP_ID != 0xFFFF
/* --- stop trigger -------------------------------------------------------- */
static bool handle_stop(const struct can_frame *frame)
{
	printing_active = false;
	LOG_INF("Stop trigger received (id=0x%03x), printing disabled", frame->id);
	return true;
}

CAN_ID_HANDLER_DEFINE(handler_stop, CONFIG_CAN_UART_REPLAY_STOP_ID, handle_stop);
#endif

/* -------------------------------------------------------------------------
 * RX thread
 * -------------------------------------------------------------------------
 */
static void can_rx_thread(void *arg1, void *arg2, void *arg3)
{
	struct can_frame frame;
	char timestamp[FORMAT_UPTIME_HMS_LEN];
	char line[96];
	int err;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		err = k_msgq_get(&can_rx_msgq, &frame, K_FOREVER);
		if (err != 0) {
			LOG_ERR("Failed to get CAN frame from msgq (err %d)", err);
			continue;
		}

		/* Walk the registered CAN ID handlers. */
		bool consumed = false;
		__maybe_unused struct can_id_handler *h;

		STRUCT_SECTION_FOREACH(can_id_handler, h) {
			if (frame.id == h->id) {
				consumed = h->fn(&frame);
				break;
			}
		}

		if (consumed) {
			continue;
		}

		if (!printing_active) {
			LOG_DBG("Frame id=0x%03x ignored (printing inactive)", frame.id);
			continue;
		}

		format_uptime_hms(timestamp, k_uptime_get());
		LOG_HEXDUMP_INF(frame.data, frame.dlc, "RX");

		format_can_frame_line(line, sizeof(line), timestamp, &frame);
		LOG_INF("%s", line);
		uart_write_str(line);
	}
}

K_THREAD_STACK_DEFINE(can_rx_stack, CONFIG_CAN_UART_REPLAY_THREAD_STACK_SIZE);
static struct k_thread can_rx_thread_data;

static int can_uart_replay_init(void)
{
	const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
	int filter_id;
	int err;

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	if (!device_is_ready(can_dev)) {
		LOG_ERR("CAN device not ready");
		return -ENODEV;
	}

	err = can_start(can_dev);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("Failed to start CAN controller (err %d)", err);
		return err;
	}

	LOG_INF("CAN bitrate: %u bit/s",
		(uint32_t)DT_PROP_OR(DT_CHOSEN(zephyr_canbus), bitrate,
				     DT_PROP_OR(DT_CHOSEN(zephyr_canbus), bus_speed, 0)));

	/*
	 * Widen the hardware filter mask so that every configured trigger ID
	 * passes through regardless of the user-configured ID/mask range.
	 * For each trigger ID, clear the bits in the mask that differ between
	 * the trigger ID and the filter base ID, forcing those bits to be
	 * ignored by the hardware.
	 */
#if CONFIG_CAN_UART_REPLAY_START_ID != 0xFFFF
	can_rx_filter.mask &= ~(CONFIG_CAN_UART_REPLAY_START_ID ^ CONFIG_CAN_UART_REPLAY_FILTER_ID);
#endif
#if CONFIG_CAN_UART_REPLAY_STOP_ID != 0xFFFF
	can_rx_filter.mask &= ~(CONFIG_CAN_UART_REPLAY_STOP_ID ^ CONFIG_CAN_UART_REPLAY_FILTER_ID);
#endif
#if CONFIG_CAN_UART_REPLAY_HELLO_SPECIALIZED_ID != 0xFFFF
	can_rx_filter.mask &=
		~(CONFIG_CAN_UART_REPLAY_HELLO_SPECIALIZED_ID ^ CONFIG_CAN_UART_REPLAY_FILTER_ID);
#endif
	filter_id = can_add_rx_filter_msgq(can_dev, &can_rx_msgq, &can_rx_filter);
	if (filter_id < 0) {
		LOG_ERR("Failed to add CAN RX filter (err %d)", filter_id);
		return filter_id;
	}

	LOG_INF("CAN RX ready, filter_id=%d mask=0x%03x", filter_id, can_rx_filter.mask);

	k_thread_create(&can_rx_thread_data, can_rx_stack, K_THREAD_STACK_SIZEOF(can_rx_stack),
			can_rx_thread, NULL, NULL, NULL, CONFIG_CAN_UART_REPLAY_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&can_rx_thread_data, "can_uart_replay");

	return 0;
}

SYS_INIT(can_uart_replay_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
