#ifndef MOTION_CONTROL_EVENTS_H
#define MOTION_CONTROL_EVENTS_H

enum motion_control_event_type {
	MOTION_CONTROL_EVENT_SET_X,
	MOTION_CONTROL_EVENT_GET_X,
	MOTION_CONTROL_EVENT_MAX,
};

enum motion_control_event_handler_type {
	MOTION_CONTROL_EVENT_HANDLER_RX,
	MOTION_CONTROL_EVENT_HANDLER_TX,
};

struct motion_control_event_handler {
	int (*handler)(int);
};

int motion_control_event_post(enum motion_control_event_type event,
			      enum motion_control_event_handler_type, int data);

int motion_control_add_event_handler(enum motion_control_event_type event,
				     enum motion_control_event_handler_type, int (*handler)(int));

#endif
