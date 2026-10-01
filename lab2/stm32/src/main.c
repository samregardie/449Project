/*
 * Encoder test: prints both counts every 200 ms. Spin each wheel by hand.
 * Watch the console (115200 baud on the ST-Link USB port).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "blinker.h"
#include "drive.h"
#include "current.h"

// TESTING ONLY - REMOVE
#include "encoder.h"
#include "motor.h"

#define PAST_LEFT (-2 * BLINKER_TURN_THRESHOLD)
#define PAST_RIGHT (2 * BLINKER_TURN_THRESHOLD)

int main(void)
{
	if (blinker_init() < 0)
	{
		printk("blinker_init failed\n");
		return 0;
	}

	/* Brakes both motors, then sets up the encoders */
	if (drive_init() < 0)
	{
		printk("drive_init failed\n");
		return 0;
	}

	// test, feel free to delete
	// blinker_set_error(true);

	current_sense_init();

	k_msleep(1000);

	// motor_set(SIDE_LEFT, 1000);

	while (true)
	{
		k_msleep(200);
		print_all_currents();

		printf("\n%d, %d, %d\n", get_current(MOTOR_LEFT), get_current(MOTOR_RIGHT), get_current(SERVO));
	}

	return 0;
}
