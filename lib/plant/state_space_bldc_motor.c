/**
 * SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app/subsys/state_space_model.h>

/* Ask the audience as to how can we modularize these values over here */
void state_space_model_init(struct state_space_model * model) {
	double J = 3.2284e-6, b = 3.5077e-6, K = 0.0274, R = 4.0, L = 2.75e-6;

	model->A[0][0] = 0.0;  model->A[0][1] = 1.0;     model->A[0][2] = 0.0;
	model->A[1][0] = 0.0;  model->A[1][1] = -b/J;    model->A[1][2] = K/J;
	model->A[2][0] = 0.0;  model->A[2][1] = -K/L;    model->A[2][2] = -R/L;

	model->B[0] = 0.0;     model->B[1] = 0.0;        model->B[2] = 1.0/L;
	model->C[0] = 1.0;     model->C[1] = 0.0;        model->C[2] = 0.0;

	for(int i=0; i<3; i++) model->x[i] = 0.0;
	model->u = 0.0f; model->y = 0.0f;
}

void state_space_model_step(struct state_space_model* model, double dt) { // dt is double
	double dx[3] = {0};
	for (int i = 0; i < 3; i++) {
		dx[i] = model->B[i] * (double)model->u;
		for (int j = 0; j < 3; j++) dx[i] += model->A[i][j] * model->x[j];
	}
	for (int i = 0; i < 3; i++) model->x[i] += dx[i] * dt;

	double temp_y = 0.0;
	for (int i = 0; i < 3; i++) temp_y += model->C[i] * model->x[i];
	model->y = (float)temp_y; // Cast back to float for the sensor
}
