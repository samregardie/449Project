#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>

#include "encoder.h"	/* enum side */

/* Full duty; motor_set() takes -MOTOR_DUTY_MAX..MOTOR_DUTY_MAX */
#define MOTOR_DUTY_MAX 1000

/* Configure PWM, DIR and DIR_A pins; both motors start braked */
int motor_init(void);

/* Positive = forward; out-of-range values are clamped */
void motor_set(enum side s, int32_t duty);

/* Both motors: PWM off and dynamic braking */
void motor_brake(void);

#endif
