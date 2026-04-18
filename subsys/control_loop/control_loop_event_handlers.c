#include <app/subsys/motion_control_events.h>
#include <app/subsys/control_loop.h>

#include <zephyr/init.h>
#include <zephyr/sys/printk.h>

static int set_position_event_handler(int position)
{
	printk("Event Handler: Set Position to %d\n", position);
	/* Here you would add code to update the control loop's setpoint */
	control_loop_set_target_setpoint(position);
	return 0;
}

static int control_loop_event_handlers_init(void)
{
	motion_control_add_event_handler(MOTION_CONTROL_EVENT_SET_X,
					 MOTION_CONTROL_EVENT_HANDLER_RX,
					 set_position_event_handler);
	return 0;
}

SYS_INIT(control_loop_event_handlers_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
