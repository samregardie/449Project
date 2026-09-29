/*
 * Encoder test: prints both counts every 200 ms. Spin each wheel by hand.
 * Watch the console (115200 baud on the ST-Link USB port).
 */

#include <zephyr/kernel.h>

#include "blinker.h"
<<<<<<< Updated upstream
#include "drive.h"

// TESTING ONLY - REMOVE
#include "encoder.h"
#include "motor.h"
	== == ==
	=
#include "current.h"
		>>>>>>> Stashed changes

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
	blinker_set_error(true);

	current_sense_init();

	while (true)
	{
		k_msleep(100);
		print_all_currents();
	}

	return 0;
}
