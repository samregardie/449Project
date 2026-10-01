/*
 * Encoder test: prints both counts every 200 ms. Spin each wheel by hand.
 * Watch the console (115200 baud on the ST-Link USB port).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "blinker.h"
#include "drive.h"
#include "current.h"



int main(void)
{
	/* All start in the error state: hazards, braked, servo centred */
	if (blinker_init() < 0 || drive_init() < 0 || servo_init() < 0) {
		printk("init failed\n");
		return 0;
	}


	return 0;
}
