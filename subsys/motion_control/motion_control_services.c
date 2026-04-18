/**
 * @file motion_control_services.c
 * The scope of this file is to provide motion control services.
 * This file serves as a layer between a higher level application running on a PC and the lower
 * level motion control services running on an embedded platform. Constraints: Use as less zephyr
 * internals as possible, and make the code as portable as possible. Services provided by this file
 * include:
 * 1. set_sync_single_axis_position(x): sets the position of a single axis to x.
 * 2. get_sync_single_axis_position(): gets the position of a single axis.
 *
 * These services shall dock on to a mqtt or grpc client running on the embedded platform.
 * MQTT:
 * These services shall dock on to a mqtt client at compile time.
 */

/**
 * The interface
 */

#include <app/subsys/motion_control_events.h>
#include <app/subsys/motion_control_services_register.h>

#include "zephyr/sys/printk.h"

struct set_sync_axis_0_position_data {
	int position;
};

static const struct json_obj_descr set_sync_axis_0_position_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct set_sync_axis_0_position_data, position, JSON_TOK_NUMBER),
};

/* Ask the audience if mqtt client is blocked or not? */
static int set_sync_axis_0_position(void *service_data)
{
	struct set_sync_axis_0_position_data *data = (struct set_sync_axis_0_position_data *)service_data;
	int position = data->position;
	printk("Service %s Invoked %d \n ", __func__, position);
	motion_control_event_post(MOTION_CONTROL_EVENT_SET_X, MOTION_CONTROL_EVENT_HANDLER_RX,
				  position);
	return 0;
}

static int get_sync_axis_0_position(void *service_data)
{
	ARG_UNUSED(service_data);
	printk("Service %s Invoked\n", __func__);
	motion_control_event_post(MOTION_CONTROL_EVENT_GET_X, MOTION_CONTROL_EVENT_HANDLER_RX, 0);
	/* Attach a listener to the event handler for this event */
	return 0;
}

REGISTER_MOTION_CONTROL_SERVICE(set_x, STRINGIFY(motion_control/commands/set_x),
						 set_sync_axis_0_position,
						 set_sync_axis_0_position_descr,
						 ARRAY_SIZE(set_sync_axis_0_position_descr));
