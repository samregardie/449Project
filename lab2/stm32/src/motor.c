/*
 * L298N H-bridge. ENA/ENB are tied high; IN1/IN3 take PWM, IN2/IN4 are DIR.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

#include "motor.h"

#define USER_NODE DT_PATH(zephyr_user)

/* Indexed by enum side */
static const struct pwm_dt_spec pwm[] = {
	[SIDE_LEFT] = PWM_DT_SPEC_GET_BY_NAME(USER_NODE, motor_left),
	[SIDE_RIGHT] = PWM_DT_SPEC_GET_BY_NAME(USER_NODE, motor_right),
};

static const struct gpio_dt_spec dir[] = {
	[SIDE_LEFT] = GPIO_DT_SPEC_GET(USER_NODE, motor_left_dir_gpios),
	[SIDE_RIGHT] = GPIO_DT_SPEC_GET(USER_NODE, motor_right_dir_gpios),
};

/* Test points */
static const struct gpio_dt_spec tp_dir_a = GPIO_DT_SPEC_GET(USER_NODE, dir_a_gpios);
static const struct gpio_dt_spec tp_pwm_set = GPIO_DT_SPEC_GET(USER_NODE, pwm_set_gpios);

int motor_init(void)
{
	const struct gpio_dt_spec *outputs[] = { &dir[SIDE_LEFT], &dir[SIDE_RIGHT], &tp_dir_a, &tp_pwm_set };

	for (int i = 0; i < ARRAY_SIZE(outputs); i++) {
		if (!gpio_is_ready_dt(outputs[i])) {
			return -ENODEV;
		}

		int ret = gpio_pin_configure_dt(outputs[i], GPIO_OUTPUT_INACTIVE);

		if (ret < 0) {
			return ret;
		}
	}

	/* PWM low and DIR low with EN tied high: both motors start braked */
	for (int i = 0; i < ARRAY_SIZE(pwm); i++) {
		if (!pwm_is_ready_dt(&pwm[i])) {
			return -ENODEV;
		}

		int ret = pwm_set_pulse_dt(&pwm[i], 0);

		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

/*
 * The motors face opposite ways, so the same polarity turns the wheels in
 * opposite directions. Set true for a side whose wheel runs backwards.
 */
static const bool reversed[] = {
	[SIDE_LEFT] = false,
	[SIDE_RIGHT] = false,
};

/* DIR_A mirrors the channel A direction pin (IN2, right motor) */
static void write_dir(enum side s, bool high)
{
	gpio_pin_set_dt(&dir[s], high);

	if (s == SIDE_RIGHT) {
		gpio_pin_set_dt(&tp_dir_a, high);
	}
}

/*
 * With EN high, IN1 = IN2 is a brake and IN1 != IN2 drives:
 *   DIR low:  PWM high drives, PWM low brakes -> pulse = duty
 *   DIR high: PWM low drives, PWM high brakes -> pulse = MAX - duty
 */
void motor_set(enum side s, int32_t duty)
{
	duty = CLAMP(duty, -MOTOR_DUTY_MAX, MOTOR_DUTY_MAX);

	if (reversed[s]) {
		duty = -duty;
	}

	bool dir_high = duty < 0;
	uint32_t on = dir_high ? MOTOR_DUTY_MAX + duty : duty;
	uint32_t pulse = (uint64_t)pwm[s].period * on / MOTOR_DUTY_MAX;

	write_dir(s, dir_high);
	pwm_set_pulse_dt(&pwm[s], pulse);
	gpio_pin_toggle_dt(&tp_pwm_set);
}

/* IN1 = IN2 = low: PWM off and both motor terminals shorted together */
void motor_brake(void)
{
	for (int s = 0; s < ARRAY_SIZE(pwm); s++) {
		pwm_set_pulse_dt(&pwm[s], 0);
		write_dir(s, false);
	}

	gpio_pin_toggle_dt(&tp_pwm_set);
}
