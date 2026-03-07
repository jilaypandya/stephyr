#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(can_uart_replay, CONFIG_CAN_UART_REPLAY_LOG_LEVEL);

#define CAN_RX_MSGQ_MAX_FRAMES 16

CAN_MSGQ_DEFINE(can_rx_msgq, CAN_RX_MSGQ_MAX_FRAMES);

/* Accept all frames (standard and extended IDs) */
static const struct can_filter can_rx_filter = {
	.id    = 0,
	.mask  = 0,
	.flags = 0,
};

static const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_uart_can));

/**
 * @brief Format millisecond uptime into "hh:mm:ss.mmm" in @p buf.
 *
 * @param buf     Output buffer (must be at least 13 bytes: "hh:mm:ss.mmm\0").
 * @param buf_len Size of @p buf.
 * @param uptime_ms Uptime in milliseconds (from k_uptime_get()).
 */
static void format_uptime_hms(char *buf, size_t buf_len, int64_t uptime_ms)
{
	int32_t ms  = (int32_t)(uptime_ms % 1000);
	int64_t sec = uptime_ms / 1000;
	int32_t s   = (int32_t)(sec % 60);
	int64_t min = sec / 60;
	int32_t m   = (int32_t)(min % 60);
	int32_t h   = (int32_t)(min / 60);

	snprintf(buf, buf_len, "%02d:%02d:%02d.%03d", h, m, s, ms);
}

static void uart_write_str(const char *str)
{
	while (*str) {
		uart_poll_out(uart_dev, *str++);
	}
}

static void can_rx_thread(void *arg1, void *arg2, void *arg3)
{
	struct can_frame frame;
	char timestamp[13]; /* "hh:mm:ss.mmm\0" */
	char line[96];
	int err;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	/*
	 * 0xFFFF is the sentinel meaning "not configured".
	 * printing_active starts false only when a real start-trigger ID is set.
	 */
	bool printing_active = (CONFIG_CAN_UART_REPLAY_START_ID == 0xFFFF);

	while (true) {
		err = k_msgq_get(&can_rx_msgq, &frame, K_FOREVER);
		if (err != 0) {
			LOG_ERR("Failed to get CAN frame from msgq (err %d)", err);
			continue;
		}

		/* --- start trigger ---------------------------------------- */
		if (frame.id == CONFIG_CAN_UART_REPLAY_START_ID) {
			printing_active = true;
			LOG_INF("Start trigger received (id=0x%03x), printing enabled",
				frame.id);
		}

		/* --- stop trigger ----------------------------------------- */
		if (frame.id == CONFIG_CAN_UART_REPLAY_STOP_ID) {
			printing_active = false;
			LOG_INF("Stop trigger received (id=0x%03x), printing disabled",
				frame.id);
		}

		if (!printing_active) {
			LOG_INF("Frame received (id=0x%03x) but printing is disabled, ignoring",
				frame.id);
			continue;
		}

		format_uptime_hms(timestamp, sizeof(timestamp), k_uptime_get());

		LOG_HEXDUMP_INF(frame.data, frame.dlc, "RX");

		snprintf(line, sizeof(line),
			 "[%s] RX id=0x%03x dlc=%u"
			 " data=%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
			 timestamp,
			 frame.id, frame.dlc,
			 frame.data[0], frame.data[1], frame.data[2], frame.data[3],
			 frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
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

	filter_id = can_add_rx_filter_msgq(can_dev, &can_rx_msgq, &can_rx_filter);
	if (filter_id < 0) {
		LOG_ERR("Failed to add CAN RX filter (err %d)", filter_id);
		return filter_id;
	}

	LOG_INF("CAN RX ready, filter_id=%d", filter_id);

	k_thread_create(&can_rx_thread_data, can_rx_stack,
			K_THREAD_STACK_SIZEOF(can_rx_stack),
			can_rx_thread, NULL, NULL, NULL,
			CONFIG_CAN_UART_REPLAY_THREAD_PRIORITY,
			0, K_NO_WAIT);
	k_thread_name_set(&can_rx_thread_data, "can_uart_replay");

	return 0;
}

SYS_INIT(can_uart_replay_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
