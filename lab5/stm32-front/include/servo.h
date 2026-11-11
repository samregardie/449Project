#ifndef SERVO_H
#define SERVO_H

#include <stdbool.h>
#include <stdint.h>

/* Steering: -100 = full left, 0 = centre, 100 = full right */
#define SERVO_STEER_MAX 100

/*
 * pulse = CENTRE + RANGE * steering / 100, in us.
 * CENTRE: wheels straight. RANGE: centre to full lock
 */
#define SERVO_PULSE_CENTRE_US 1500
#define SERVO_PULSE_RANGE_US 700

/* Outputs centre; starts in the error state */
int servo_init(void);

/* Clamped to -100..100; ignored in the error state. ISR-safe. */
void servo_command(int32_t steering);

/* true = hold the current position and ignore commands */
void servo_set_error(bool error);

#endif
