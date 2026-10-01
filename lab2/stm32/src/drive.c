/*
 * Velocity control: throttle -> target velocity, PI on the average of the two
 * encoders, brake beats throttle.
 *
 * drive_command() and drive_set_error() only store the request. The control
 * thread is the only code that writes to the motors: every 1 ms it samples
 * the encoders, reads the request and either brakes or runs the PI. The 1 ms
 * period keeps command -> PWM_SET under the 2 ms limit.
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

/* Linear: full throttle = 30 ticks per 10 ms (~40% of the measured max) */
#define MAX_VELOCITY 30.0f
#define THROTTLE_DEADBAND 2	/* |throttle| below this (i.e. +/-1%) means 0 */

/* PI gains: velocity error (ticks per 10 ms) -> duty. TODO (team): tune */
#define KP 20.0f
#define KI 100.0f

#define DT (ENCODER_SAMPLE_MS / 1000.0f)	/* seconds per tick */

static const struct gpio_dt_spec tp_pwm_set =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pwm_set_gpios);

/* Shared between the control thread and callers, always under the lock */
static struct k_spinlock lock;
static int32_t throttle;
static bool brake;
static bool error = true;	/* power-up is the error state */
static uint32_t seq;		/* bumped on every accepted command */
static struct drive_status status;

/* Only touched by the control thread */
static float integral;

static float throttle_to_target(int32_t t)
{
	if (abs(t) < THROTTLE_DEADBAND) {
		return 0.0f;
	}

	return t * MAX_VELOCITY / DRIVE_THROTTLE_MAX;
}

static int32_t pi_update(float target, float velocity)
{
	float err = target - velocity;
	float out = KP * err + KI * integral;

	/* Anti-windup: don't integrate further into saturation */
	bool saturated = (out >= MOTOR_DUTY_MAX && err > 0) ||
			 (out <= -MOTOR_DUTY_MAX && err < 0);

	if (!saturated) {
		integral += err * DT;
	}

	return (int32_t)CLAMP(out, -MOTOR_DUTY_MAX, MOTOR_DUTY_MAX);
}

K_SEM_DEFINE(start_sem, 0, 1);
K_TIMER_DEFINE(loop_timer, NULL, NULL);

static void control_loop(void *p1, void *p2, void *p3)
{
	uint32_t applied_seq = 0;

	/* Wait until drive_init() has set up the motors and encoders */
	k_sem_take(&start_sem, K_FOREVER);
	k_timer_start(&loop_timer, K_MSEC(ENCODER_SAMPLE_MS), K_MSEC(ENCODER_SAMPLE_MS));

	while (1) {
		k_timer_status_sync(&loop_timer);
		encoder_sample();

		k_spinlock_key_t key = k_spin_lock(&lock);
		int32_t t = throttle;
		bool stop = brake || error;
		uint32_t s = seq;

		k_spin_unlock(&lock, key);

		float velocity = (encoder_velocity(SIDE_LEFT) + encoder_velocity(SIDE_RIGHT)) / 2.0f;
		float target = 0.0f;
		int32_t duty = 0;

		if (stop) {
			motor_brake();
			integral = 0.0f;	/* so releasing the brake doesn't lurch */
		} else {
			target = throttle_to_target(t);
			duty = pi_update(target, velocity);
			motor_set(SIDE_LEFT, duty);
			motor_set(SIDE_RIGHT, duty);
		}

		/* First write after a new command: mark it for the scope */
		if (s != applied_seq) {
			gpio_pin_toggle_dt(&tp_pwm_set);
			applied_seq = s;
		}

		key = k_spin_lock(&lock);
		status = (struct drive_status){ target, velocity, duty };
		k_spin_unlock(&lock, key);
	}
}

/* Preemptive: higher-priority threads and ISRs can interrupt a tick */
K_THREAD_DEFINE(drive_thread, 1024, control_loop, NULL, NULL, NULL, K_PRIO_PREEMPT(2), 0, 0);

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
		throttle = CLAMP(new_throttle, -DRIVE_THROTTLE_MAX, DRIVE_THROTTLE_MAX);
		brake = new_brake;
		seq++;
	}

	k_spin_unlock(&lock, key);
}

void drive_set_error(bool new_error)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Either way, start again from a standstill */
	error = new_error;
	throttle = 0;
	brake = false;
	seq++;

	k_spin_unlock(&lock, key);
}

struct drive_status drive_get_status(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	struct drive_status s = status;

	k_spin_unlock(&lock, key);
	return s;
}
