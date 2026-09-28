/*
 * Blinker test: steps through the blinker states, printing each one.
 * Watch the LEDs next to the console (115200 baud on the ST-Link USB port).
 */

#include <zephyr/kernel.h>

#include "blinker.h"
#include "drive.h"

// TESTING ONLY - REMOVE
#include "motor.h"

#define PAST_LEFT  (-2 * BLINKER_TURN_THRESHOLD)
#define PAST_RIGHT (2 * BLINKER_TURN_THRESHOLD)

int main(void)
{
	if (blinker_init() < 0) {
		printk("blinker_init failed\n");
		return 0;
	}

	// if (drive_init() < 0) {
	// 	printk("drive_init failed\n");
	// 	return 0;
	// }

	// TESTING ONLY - REMOVE
	motor_init();

	motor_set(SIDE_LEFT, 300);
	k_msleep(3000);

	motor_brake();
	k_msleep(1000);

	motor_set(SIDE_LEFT, -300);
	k_msleep(3000);

	motor_brake();

	motor_set(SIDE_RIGHT, 300);
	k_msleep(3000);

	motor_brake();
	k_msleep(1000);

	motor_set(SIDE_RIGHT, -300);
	k_msleep(3000);

	motor_brake();

	return 0;
}
