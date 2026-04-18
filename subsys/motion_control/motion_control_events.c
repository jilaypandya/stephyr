/**
 * The scope of this file is to provide a layer to catapault motion control events from a service
 * handler further to control loop subsystem.
 */

#include <zephyr/kernel.h>

#include <app/subsys/motion_control_events.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(motion_control_events, LOG_LEVEL_INF);

struct motion_control_events_data {
	enum motion_control_event_type type;
	enum motion_control_event_handler_type tx_rx;
	int data;
};

/* Can somebody say the initialized value of all the handler routines and why? */
static struct motion_control_event_handler rx_event_handlers[MOTION_CONTROL_EVENT_MAX];
static struct motion_control_event_handler tx_event_handlers[MOTION_CONTROL_EVENT_MAX];

K_MSGQ_DEFINE(motion_control_events_queue, sizeof(struct motion_control_events_data),
	      MOTION_CONTROL_EVENT_MAX, 1);

int motion_control_event_post(enum motion_control_event_type event,
			      enum motion_control_event_handler_type handler_type, int data)
{
	struct motion_control_events_data _event = {
		.type = event,
		.tx_rx = handler_type,
		.data = data,
	};
	k_msgq_put(&motion_control_events_queue, &_event, K_NO_WAIT);
	printk("Motion control event posted %d \n", event);
	return 0;
}

int motion_control_add_event_handler(enum motion_control_event_type event,
				     enum motion_control_event_handler_type rx_tx_event,
				     int (*handler)(int))
{
	if (event >= MOTION_CONTROL_EVENT_MAX) {
		return -EINVAL;
	}

	switch (rx_tx_event) {
	case MOTION_CONTROL_EVENT_HANDLER_RX:
		LOG_INF("Adding rx handler for event %d \n", event);
		rx_event_handlers[event] = (struct motion_control_event_handler){
			.handler = handler,
		};
		break;
	case MOTION_CONTROL_EVENT_HANDLER_TX:
		tx_event_handlers[event] = (struct motion_control_event_handler){
			.handler = handler,
		};
		break;
	}

	return 0;
}

static void motion_control_events_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	for (;;) {
		struct motion_control_events_data event;

		/* Threads waits/spins forever waiting for a new event */
		k_msgq_get(&motion_control_events_queue, &event, K_FOREVER);

		switch (event.tx_rx) {
		case MOTION_CONTROL_EVENT_HANDLER_RX:
			if (rx_event_handlers[event.type].handler != NULL) {
				rx_event_handlers[event.type].handler(event.data);
			} else {
				printk("No rx handler registered for event %d \n", event.type);
			}
			break;
		case MOTION_CONTROL_EVENT_HANDLER_TX:
			if (tx_event_handlers[event.type].handler != NULL) {
				tx_event_handlers[event.type].handler(event.data);
			}
			printk("No tx handler registered for event %d \n", event.type);
			break;
		}
	}
}

K_THREAD_DEFINE(motion_control_events_thread, 1024, motion_control_events_thread_fn, NULL, NULL,
		NULL, CONFIG_APPLICATION_INIT_PRIORITY, 0, 0);
