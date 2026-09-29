/*
 * Velocity control: throttle -> target velocity, PID on the average of the
 * two encoders, brake beats throttle.
 *
 * drive_command() and drive_set_error() only store the request. The control
 * thread is the only code that writes to the motors: every ENCODER_SAMPLE_MS
 * it samples the encoders, reads the request and either brakes or runs the
 * controller. A 1 ms period keeps command -> PWM_SET under the 2 ms limit.
 *
 * PWM_SET toggles only on the tick that first applies a new command, so
 * CMD_RX -> PWM_SET on the scope is the software response time.
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "drive.h"
#include "encoder.h"
#include "motor.h"

/* Cooperative: runs each tick to completion, only ISRs can interrupt it */
#define LOOP_PRIORITY K_PRIO_COOP(2)
#define LOOP_STACK 1024

/* Shared with callers of drive_command()/drive_set_error() */
static struct k_spinlock lock;
static int32_t throttle;	/* -MOTOR_DUTY_MAX..MOTOR_DUTY_MAX */
static bool brake;
static bool error = true;	/* power-up is the error state */
static uint32_t seq;		/* bumped on every command or error change */

static const struct gpio_dt_spec tp_pwm_set =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pwm_set_gpios);

/* Linear, monotonic: full throttle = 30 ticks per 10 ms (~40% of the measured max) */
#define MAX_VELOCITY 30.0f
#define THROTTLE_DEADBAND 5	/* |cmd| below this means 0 */

/* TODO (team): tune. Start with KI = 0 and raise KP, then add KI */
#define KP 0.0f
#define KI 0.0f

#define DT (ENCODER_SAMPLE_MS / 1000.0f)	/* seconds per update */

/* Only touched by the control thread */
static float integral;

/* Called on every braked tick, so releasing the brake doesn't lurch */
static void pid_reset(void)
{
	integral = 0.0f;
}

static float throttle_to_target(int32_t cmd)
{
	if (abs(cmd) < THROTTLE_DEADBAND) {
		return 0.0f;
	}

	return cmd * MAX_VELOCITY / MOTOR_DUTY_MAX;
}

/* PI on velocity (ticks per 10 ms); returns the duty */
static int32_t pid_update(int32_t cmd, int32_t velocity)
{
	float error = throttle_to_target(cmd) - velocity;
	float out = KP * error + KI * integral;

	/* Anti-windup: stop integrating while saturated and error pushes further */
	bool saturated = (out >= MOTOR_DUTY_MAX && error > 0) ||
			 (out <= -MOTOR_DUTY_MAX && error < 0);

	if (!saturated) {
		integral += error * DT;
	}

	return (int32_t)CLAMP(out, -MOTOR_DUTY_MAX, MOTOR_DUTY_MAX);
}

K_SEM_DEFINE(start_sem, 0, 1);
K_TIMER_DEFINE(loop_timer, NULL, NULL);

static void control_loop(void *p1, void *p2, void *p3)
{
	/* Wait until drive_init() has set up the motors and encoders */
	k_sem_take(&start_sem, K_FOREVER);
	k_timer_start(&loop_timer, K_MSEC(ENCODER_SAMPLE_MS), K_MSEC(ENCODER_SAMPLE_MS));

	uint32_t applied_seq = 0;

	while (1) {
		k_timer_status_sync(&loop_timer);
		encoder_sample();

		k_spinlock_key_t key = k_spin_lock(&lock);
		int32_t t = throttle;
		bool stop = brake || error;
		uint32_t s = seq;

		k_spin_unlock(&lock, key);

		if (stop) {
			motor_brake();
			pid_reset();
		} else {
			/* TODO: average of both sides once the right encoder works */
			int32_t duty = pid_update(t, encoder_velocity(SIDE_LEFT));

			motor_set(SIDE_LEFT, duty);
			motor_set(SIDE_RIGHT, duty);
		}

		/* First write after a new command: mark it for the scope */
		if (s != applied_seq) {
			gpio_pin_toggle_dt(&tp_pwm_set);
			applied_seq = s;
		}
	}
}

K_THREAD_DEFINE(drive_thread, LOOP_STACK, control_loop, NULL, NULL, NULL,
		LOOP_PRIORITY, 0, 0);

int drive_init(void)
{
	/* Motors first, so they're braked before anything else runs */
	int ret = motor_init();

	if (ret < 0) {
		return ret;
	}

	if (!gpio_is_ready_dt(&tp_pwm_set)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&tp_pwm_set, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	ret = encoder_init();
	if (ret < 0) {
		return ret;
	}

	k_sem_give(&start_sem);
	return 0;
}

void drive_command(int32_t new_throttle, bool new_brake)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Ignored in the error state */
	if (!error) {
		throttle = CLAMP(new_throttle, -MOTOR_DUTY_MAX, MOTOR_DUTY_MAX);
		brake = new_brake;
		seq++;
	}

	k_spin_unlock(&lock, key);
}

void drive_set_error(bool new_error)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	error = new_error;
	seq++;

	/* Leaving the error state starts from a standstill */
	throttle = 0;
	brake = false;

	k_spin_unlock(&lock, key);
}
