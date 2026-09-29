/*
 * Velocity control: throttle -> target velocity, PID on the average of the
 * two encoders, brake beats throttle.
 */

#include <zephyr/kernel.h>

#include "drive.h"
#include "encoder.h"
#include "motor.h"

int drive_init(void)
{
	/* Motors first, so they're braked before anything else runs */
	int ret = motor_init();

	if (ret < 0) {
		return ret;
	}

	ret = encoder_init();
	if (ret < 0) {
		return ret;
	}

	/* TODO: start the control thread */
	drive_set_error(true);
	return 0;
}

void drive_command(int32_t throttle, bool brake)
{
	/* TODO */
}

void drive_set_error(bool error)
{
	/* TODO */
}
