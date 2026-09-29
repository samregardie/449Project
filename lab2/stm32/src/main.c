/*
 * PI tuning: holds TUNE_THROTTLE and prints "target_x10,velocity_x10,duty"
 * every 20 ms. Graph it live with pid_live.py and load the wheel by hand.
 * Wheels off the ground (or on the floor once it's stable).
 */

#include <zephyr/kernel.h>

#include "blinker.h"
#include "drive.h"

// TESTING ONLY - REMOVE
#define TUNE_THROTTLE 500

int main(void)
{
	if (blinker_init() < 0) {
		printk("blinker_init failed\n");
		return 0;
	}

	/* Brakes both motors, then sets up the encoders */
	if (drive_init() < 0) {
		printk("drive_init failed\n");
		return 0;
	}

	// TESTING ONLY - REMOVE
	drive_set_error(false);
	drive_command(TUNE_THROTTLE, false);

	while (1) {
		struct drive_status s = drive_get_status();

		/* printk has no %f, so x10 */
		printk("%d,%d,%d\n", (int)(s.target * 10), (int)(s.velocity * 10), s.duty);
		k_msleep(20);
	}

	return 0;
}
