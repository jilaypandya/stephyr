/**
 * SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <app/subsys/mqtt_client.h>
#include <app/subsys/motion_control_services_register.h>
#include <zephyr/init.h>
#include <zephyr/data/json.h>

#include "zephyr/sys/iterable_sections.h"
#include "zephyr/sys/printk.h"

#ifdef CONFIG_MQTT_PUB_SUB_CLIENT

/* MQTT Publishes messages per topics. Subscribers to those topics get notified */
/* This listener layer is required in between since mqtt is not a rpc framework */
/* If a RPC framework such as gRPC is to be used, underlying the services could be called directly
 */
int motion_control_service_listener(const uint8_t *topic, const char *payload)
{
	printk("%s received topic: %s, payload: %s", __func__, (char *)topic, payload);

	STRUCT_SECTION_FOREACH(motion_control_service, service) {
		if (strcmp(service->name, (char *)topic) == 0) {
			void *service_data;
			int ret;

			ret = json_obj_parse((char*)payload, strlen(payload), service->service_descriptor,
					     service->service_descriptor_size,
					     &service_data);
			printk("error in json obj parsing is %d \n", ret);

			service->func(&service_data);
		}
	}

	return 0;
}
#endif

#ifdef CONFIG_GRPC_STUB

int motion_control_service_listener(void)
{
	/* This is a stub for the grpc listener. */
	return 0;
}

#endif

int motion_control_register_services_init(void)
{
	mqtt_client_attach_listener(motion_control_service_listener);
	return 0;
}

SYS_INIT(motion_control_register_services_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
