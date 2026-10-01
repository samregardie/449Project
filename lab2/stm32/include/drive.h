#ifndef DRIVE_H
#define DRIVE_H

#include <stdbool.h>
#include <stdint.h>

/* Throttle: -100 = full reverse, 0 = stop, 100 = full forward */
#define DRIVE_THROTTLE_MAX 100

/* Start the velocity control loop; starts in the error state (braked) */
int drive_init(void);

/*
 * Call with every valid command from the Pi; brake beats throttle.
 * Throttle clamped to -100..100. Safe to call from a thread or an ISR.
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
