/**
 * SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */

#include "state_space_model.h"

#ifdef CONFIG_STATE_SPACE_MODEL
struct actuator {
	struct state_space_model* physical_plant;
};

struct sensor {
	struct state_space_model* physical_plant;
};

struct set_point_generator {
	double current_setpoint;
	double final_target;
	double max_speed_rad_s;
};
#endif