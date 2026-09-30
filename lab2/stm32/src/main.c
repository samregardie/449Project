/* Servo bench test and calibration: left, centre, right, centre, forever */

#include <zephyr/kernel.h>

#include "drive.h"
#include "servo.h"

// TESTING ONLY - REMOVE
#define STEP_MS 2000

int main(void)
{
	/* Brakes the motors so the L298N inputs aren't floating */
	if (drive_init() < 0 || servo_init() < 0) {
		printk("init failed\n");
		return 0;
	}

	/* No link in this test, so leave the error state by hand */
	servo_set_error(false);

	const int32_t steps[] = { -100, 0, 100, 0 };

	while (1) {
		for (int i = 0; i < ARRAY_SIZE(steps); i++) {
			printk("steering %d\n", steps[i]);
			servo_command(steps[i]);
			k_msleep(STEP_MS);
		}
	}

	return 0;
}
