/*
 * Blinker test: steps through the blinker states, printing each one.
 * Watch the LEDs next to the console (115200 baud on the ST-Link USB port).
 */

#include <zephyr/kernel.h>

#include "blinker.h"

#define PAST_LEFT  (-2 * BLINKER_TURN_THRESHOLD)
#define PAST_RIGHT (2 * BLINKER_TURN_THRESHOLD)

int main(void)
{
	if (blinker_init() < 0) {
		printk("blinker_init failed\n");
		return 0;
	}

	printk("Power-up: hazards, all four at 2 Hz\n");
	k_msleep(4000);

	printk("Error cleared: off\n");
	blinker_set_error(false);
	k_msleep(2000);

	printk("Left pressed: left blinks\n");
	blinker_left_pressed();
	k_msleep(3000);

	printk("Steer left past threshold: still left\n");
	blinker_steering(PAST_LEFT);
	k_msleep(3000);

	printk("Steer back to centre: self-cancel, off\n");
	blinker_steering(0);
	k_msleep(2000);

	printk("Right pressed: right blinks\n");
	blinker_right_pressed();
	k_msleep(3000);

	printk("Left pressed: cancels right, off\n");
	blinker_left_pressed();
	k_msleep(2000);

	printk("Right pressed, steer right past threshold: right\n");
	blinker_right_pressed();
	blinker_steering(PAST_RIGHT);
	k_msleep(3000);

	printk("Left pressed while turning right: cancels, off\n");
	blinker_left_pressed();
	k_msleep(2000);

	printk("Error: hazards\n");
	blinker_set_error(true);

	return 0;
}
