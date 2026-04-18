/**
 * SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STATE_SPACE_MODEL_H
#define STATE_SPACE_MODEL_H

struct state_space_model{
	double x[3]; // CHANGED TO DOUBLE: [position, velocity, current]
	double A[3][3], B[3], C[3]; // Matrices as double
	float u;    // Input: Voltage (V)
	float y;    // Output: Position (rad)
};

void state_space_model_init(struct state_space_model * model);

void state_space_model_step(struct state_space_model* model, double dt);

#endif
