/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/stepper.h>
#include <zephyr/logging/log.h>

#include <app/drivers/blink.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static void stepper_print_event_callback(const struct device *dev, enum stepper_event event,
					 void *user_data)
{
	const struct device *dev_callback = user_data;

	LOG_INF("Event %d, %s called for %s, expected for %s\n", event, __func__,
		dev_callback->name==NULL?"NULL":"soemthong", dev->name);
}


int main(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_ALIAS(stepper));
	stepper_set_microstep_interval(dev, 10000000);
	stepper_set_event_callback(dev, stepper_print_event_callback, NULL );
	int32_t pos;
	LOG_INF("Starting tmc50xx stepper sample");
	k_sleep(K_MSEC(1000));

	while (1) {
		stepper_move_by(dev, -10);
		stepper_stop(dev);
		stepper_get_actual_position(dev, &pos);
	}
	while (1) {
		stepper_move_by(dev, 10);
		k_sleep(K_MSEC(100));
		stepper_move_by(dev, -10);
		stepper_stop(dev);
		stepper_get_actual_position(dev, &pos);
		LOG_INF("pos: %d", pos);
		k_sleep(K_MSEC(1000));
	}

	return 0;
}

