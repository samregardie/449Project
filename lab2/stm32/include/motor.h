#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>

#include "encoder.h"	/* enum side */

/* Configure PWM, DIR and test-point pins; both motors start braked */
int motor_init(void);

/* Signed duty, -MAX..MAX (team picks MAX); toggles PWM_SET after the write */
void motor_set(enum side s, int32_t duty);

/* Both motors: PWM off and dynamic braking */
void motor_brake(void);

#endif
