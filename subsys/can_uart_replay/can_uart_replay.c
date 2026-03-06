#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <stdio.h>

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

static void uart_write_str(const char *str)
{
	while (*str) {
		uart_poll_out(uart_dev, *str++);
	}
}

static void can_rx_thread(void *arg1, void *arg2, void *arg3)
{
	struct can_frame frame;
	char line[80];
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

		LOG_INF("RX id=0x%03x dlc=%u flags=0x%02x"
			" data=%02x %02x %02x %02x %02x %02x %02x %02x",
			frame.id, frame.dlc, frame.flags,
			frame.data[0], frame.data[1], frame.data[2], frame.data[3],
			frame.data[4], frame.data[5], frame.data[6], frame.data[7]);

		snprintf(line, sizeof(line),
			 "RX id=0x%03x dlc=%u data=%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
			 frame.id, frame.dlc,
			 frame.data[0], frame.data[1], frame.data[2], frame.data[3],
			 frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
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
