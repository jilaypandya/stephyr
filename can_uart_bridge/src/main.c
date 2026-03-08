/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_CAN_UART_BRIDGE_LOG_LEVEL);

static int print_version(void)
{
	LOG_INF("can_uart_bridge v%s (build %s)", APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION));
	return 0;
}

SYS_INIT(print_version, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

int main(void)
{
	for (;;) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
