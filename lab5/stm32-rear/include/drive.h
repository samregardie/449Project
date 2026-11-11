#ifndef DRIVE_H
#define DRIVE_H

#include <stdbool.h>
#include <stdint.h>

/* Throttle: -100 = full reverse, 0 = stop, 100 = full forward */
#define DRIVE_THROTTLE_MAX 100

/* Start the velocity control loop; starts in the error state (braked) */
int drive_init(void);

/* Every valid CTRL; brake beats throttle. Ignored in the error state */
void drive_command(int32_t throttle, bool brake);

/* true = fail-safe (braked, commands ignored), false = back to normal */
void drive_set_error(bool error);

#endif
