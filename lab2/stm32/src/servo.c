/*
 * Steering servo, 50 Hz PWM. The timer repeats the pulse by itself, so no
 * thread is needed. A new width starts at the next period (<= 20 ms).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>

#include "servo.h"

static const struct pwm_dt_spec servo = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), servo);

/* Also set from the link timer ISR, so always under the lock */
static struct k_spinlock lock;
static bool error = true;	/* power-up is the error state */

/* Clamping the input keeps the pulse within CENTRE +/- RANGE */
static void write_steering(int32_t steering)
{
	steering = CLAMP(steering, -SERVO_STEER_MAX, SERVO_STEER_MAX);

	int32_t us = SERVO_PULSE_CENTRE_US +
		     SERVO_PULSE_RANGE_US * steering / SERVO_STEER_MAX;

	/* Only register writes, so fine under a spinlock */
	pwm_set_pulse_dt(&servo, PWM_USEC(us));
}

int servo_init(void)
{
	if (!pwm_is_ready_dt(&servo)) {
		return -ENODEV;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);

	write_steering(0);

	k_spin_unlock(&lock, key);
	return 0;
}

void servo_command(int32_t steering)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (!error) {
		write_steering(steering);
	}

	k_spin_unlock(&lock, key);
}

void servo_set_error(bool new_error)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	error = new_error;

	k_spin_unlock(&lock, key);
}
