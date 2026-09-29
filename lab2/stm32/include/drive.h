#ifndef DRIVE_H
#define DRIVE_H

#include <stdbool.h>
#include <stdint.h>

/* Start the velocity control loop; starts in the error state (braked) */
int drive_init(void);

/*
 * Call with every valid command from the Pi; brake beats throttle.
 * throttle: -MOTOR_DUTY_MAX..MOTOR_DUTY_MAX (clamped), positive = forward.
 * Safe to call from a thread or an ISR.
 */
void drive_command(int32_t throttle, bool brake);

/* true = fail-safe (braked, commands ignored), false = back to normal */
void drive_set_error(bool error);

/* The latest control-loop tick */
struct drive_status {
	float target;		/* ticks per 10 ms */
	float velocity;		/* ticks per 10 ms, average of both wheels */
	int32_t duty;		/* 0 while braked */
};

struct drive_status drive_get_status(void);

#endif
