#ifndef APP_LIB_STEPPER_CONTROL_H_
#define APP_LIB_STEPPER_CONTROL_H_

#include <zephyr/drivers/stepper.h>

int stepper_control_move_to(const struct device *dev,int32_t micro_steps, k_timeout_t timeout);

#endif
