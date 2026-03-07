/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Periodic random CAN traffic generator.
 *
 * Each active TX slot is driven by a k_work_delayable that reschedules itself
 * after every transmission, so no extra thread is required.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>

LOG_MODULE_REGISTER(can_random_traffic, CONFIG_CAN_UART_REPLAY_LOG_LEVEL);

/* Maximum number of TX slots supported by this implementation. */
#define CAN_RANDOM_TRAFFIC_MAX_SLOTS 3

struct tx_slot {
	struct k_work_delayable dwork;
	const struct device *can_dev;
	uint32_t id;
	uint32_t period_ms;
};

static struct tx_slot tx_slots[CAN_RANDOM_TRAFFIC_MAX_SLOTS];

/* Compile-time table of (id, period_ms) pairs derived from Kconfig. */
static const struct {
	uint32_t id;
	uint32_t period_ms;
} tx_cfg[CAN_RANDOM_TRAFFIC_MAX_SLOTS] = {
	{CONFIG_CAN_RAND_TX_MSG0_ID, CONFIG_CAN_RAND_TX_MSG0_PERIOD_MS},
#if CONFIG_CAN_RAND_TX_MSG_COUNT >= 2
	{CONFIG_CAN_RAND_TX_MSG1_ID, CONFIG_CAN_RAND_TX_MSG1_PERIOD_MS},
#else
	{0, 1000},
#endif
#if CONFIG_CAN_RAND_TX_MSG_COUNT >= 3
	{CONFIG_CAN_RAND_TX_MSG2_ID, CONFIG_CAN_RAND_TX_MSG2_PERIOD_MS},
#else
	{0, 1000},
#endif
};

static void tx_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct tx_slot *slot = CONTAINER_OF(dwork, struct tx_slot, dwork);
	struct can_frame frame = {
		.id = slot->id,
		.dlc = CAN_MAX_DLC,
		.flags = 0,
	};
	int err;

	sys_rand_get(frame.data, CAN_MAX_DLC);

	err = can_send(slot->can_dev, &frame, K_MSEC(100), NULL, NULL);
	if (err != 0) {
		LOG_WRN("TX id=0x%03x failed (err %d)", slot->id, err);
	} else {
		LOG_INF("TX id=0x%03x", slot->id);
		LOG_HEXDUMP_INF(frame.data, CAN_MAX_DLC, "data:");
	}

	/* Reschedule for the next period. */
	k_work_reschedule(dwork, K_MSEC(slot->period_ms));
}

static int can_random_traffic_init(void)
{
	const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

	if (!device_is_ready(can_dev)) {
		LOG_ERR("CAN device not ready");
		return -ENODEV;
	}

	for (int i = 0; i < CONFIG_CAN_RAND_TX_MSG_COUNT; i++) {
		tx_slots[i].can_dev = can_dev;
		tx_slots[i].id = tx_cfg[i].id;
		tx_slots[i].period_ms = tx_cfg[i].period_ms;
		k_work_init_delayable(&tx_slots[i].dwork, tx_work_handler);
		LOG_INF("Scheduling TX id=0x%03x every %u ms", tx_slots[i].id,
			tx_slots[i].period_ms);
		k_work_reschedule(&tx_slots[i].dwork, K_MSEC(tx_slots[i].period_ms));
	}
	LOG_INF("Periodic random TX enabled: %d slot(s)", CONFIG_CAN_RAND_TX_MSG_COUNT);
	return 0;
}

SYS_INIT(can_random_traffic_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
