/*
 * L298N H-bridge. ENA/ENB are tied high; IN1/IN3 take PWM, IN2/IN4 are DIR.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

#include "motor.h"

int motor_init(void)
{
	/* TODO: configure PWM, DIR and DIR_A/PWM_SET test points */
	motor_brake();
	return 0;
}

void motor_set(enum side s, int32_t duty)
{
	/* TODO */
}

void motor_brake(void)
{
	/* TODO */
}
