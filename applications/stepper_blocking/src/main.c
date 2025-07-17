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

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_ALIAS(stepper));
	stepper_set_microstep_interval(dev, 10000000);
	while (1) {
		stepper_move_by(dev, 1);
		k_sleep(K_MSEC(1000));
		stepper_move_by(dev, -1);
		k_sleep(K_MSEC(1000));
	}

	return 0;
}

