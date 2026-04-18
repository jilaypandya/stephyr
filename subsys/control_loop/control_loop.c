#include <app/subsys/state_space_model.h>
#include <app/subsys/sensor_actuator.h>
#include <stdio.h>

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(control_loop, LOG_LEVEL_INF);

/* =====================================================================
   1. STATE SPACE MODEL (The Physics Simulation)
   ===================================================================== */
/* =====================================================================
   1. STATE SPACE MODEL (The Physics Simulation)
   ===================================================================== */
/* =====================================================================
   2. ACTUATOR (Hardware Abstraction)
   ===================================================================== */

K_SEM_DEFINE(set_point_sem, 1, 1);

static struct set_point_generator trajectory;

void actuator_write_voltage(struct actuator *act, float volts)
{
	// Hardware limits applied here
	if (volts > 24.0f) {
		volts = 24.0f;
	}
	if (volts < -24.0f) {
		volts = -24.0f;
	}
	act->physical_plant->u = volts;
}

/* =====================================================================
   3. SENSOR (Hardware Abstraction)
   ===================================================================== */

float sensor_read_position(struct sensor *sen)
{
	// In reality, this reads a hardware register (e.g., QEI encoder count)
	return sen->physical_plant->y;
}

/* =====================================================================
   4. SET POINT GENERATOR (Trajectory Planner)
   ===================================================================== */

void set_point_generator_init(struct set_point_generator *spg, float target, float speed)
{
	spg->final_target = target;
	spg->max_speed_rad_s = speed;
}

float set_point_generator_update(struct set_point_generator *spg, const double dt)
{
	/* Ramp up the setpoint towards the target */
	k_sem_take(&set_point_sem, K_FOREVER);

	if (spg->current_setpoint < spg->final_target) {
		spg->current_setpoint += spg->max_speed_rad_s * dt;
	} else if (spg->current_setpoint > spg->final_target) {
		spg->current_setpoint -= spg->max_speed_rad_s * dt;
	}

	k_sem_give(&set_point_sem);
	return spg->current_setpoint;
}

/* =====================================================================
   5. CONTROL LOOP (PID Algorithm)
   ===================================================================== */
struct control_loop {
	double Kp, Ki, Kd;
	double integral_err;
	double prev_err;
};

void control_loop_init(struct control_loop *ctrl, double Kp, double Ki, double Kd)
{
	ctrl->Kp = Kp;
	ctrl->Ki = Ki;
	ctrl->Kd = Kd;
	ctrl->integral_err = 0.0f;
	ctrl->prev_err = 0.0f;
}

float control_loop_compute(struct control_loop *ctrl, double setpoint, double measurement, double dt)
{
	double error = setpoint - measurement;
	ctrl->integral_err += error * dt;
	double derivative = (error - ctrl->prev_err) / dt;
	ctrl->prev_err = error;

	return (ctrl->Kp * error) + (ctrl->Ki * ctrl->integral_err) + (ctrl->Kd * derivative);
}

float sensor_read_velocity(struct sensor *sen)
{
	// x[1] is the internal velocity state in radians per second.
	// In real hardware, this would be calculated from the encoder ticks over time,
	// or read directly from a tachometer/gyro.
	return sen->physical_plant->x[1];
}

int control_loop_set_target_setpoint(int target_setpoint)
{
	k_sem_take(&set_point_sem, K_FOREVER);

	LOG_INF("Control Loop: New target setpoint received: %d", target_setpoint);
	set_point_generator_init(&trajectory, target_setpoint, 250.0f);

	k_sem_give(&set_point_sem);
	return 0;
}

static void log_control_loop_status(double dt_ctrl, int target_pos, double desired_pos,
				    double actual_pos, double actual_vel, double command_volts)
{
	static int logging_tick = 0;
	logging_tick+=10;

	if (logging_tick % 1000 == 0) {
		/* Update your printf to include the velocity */
		LOG_INF("Time: %.1fs | Target Pos: %d | Desired Pos: %6.1f | Actual Pos: %6.1f | "
			"Speed: %6.1f "
			"rad/s | Volts: %5.2f ",
			logging_tick * dt_ctrl, target_pos, desired_pos, actual_pos, actual_vel,
			command_volts);
	}
}

/* =====================================================================
   MAIN APPLICATION (Wiring it all together)
   ===================================================================== */
static void control_loop_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	// 1. Instantiate components
	struct state_space_model motor;
	struct actuator motor_driver;
	struct sensor encoder;
	struct control_loop pid;

	// 2. Initialize and wire components
	state_space_model_init(&motor);
	motor_driver.physical_plant = &motor;
	encoder.physical_plant = &motor;

	/* Go to 1000 rad at 250 rad/s */

	/* PID Gains */
	control_loop_init(&pid, 0.5f, 0.01f, 0.005f);

	/* Timing Parameters */
	double dt_sim = 1e-6f;   // Fast simulation step (1us)
	double dt_ctrl = 0.001f; // Control loop step (1ms)
	int sim_steps = (int)(dt_ctrl / dt_sim);

	printf("Starting Modular Control System...\n");

	for (;;) {

		float desired_pos = set_point_generator_update(&trajectory, dt_ctrl);
		float actual_pos = sensor_read_position(&encoder);

		float command_volts = control_loop_compute(&pid, desired_pos, actual_pos, dt_ctrl);
		actuator_write_voltage(&motor_driver, command_volts);

		for (int i = 0; i < sim_steps; i++) {
			state_space_model_step(&motor, dt_sim);
		}

		float actual_vel = sensor_read_velocity(&encoder);
		log_control_loop_status(dt_ctrl, trajectory.final_target, desired_pos, actual_pos,
					actual_vel, command_volts);
	}
}

K_THREAD_DEFINE(control_loop_thread_id, 1024, control_loop_thread, NULL, NULL, NULL,
		CONFIG_APPLICATION_INIT_PRIORITY, 0, 0);
