#include <zephyr/kernel.h>

#include "blinker.h"
#include "drive.h"
#include "errorstate.h"
#include "servo.h"
#include "picom.h"



int main(void)
{
	/* All start in the error state: hazards, braked, servo centred */
	if (blinker_init() < 0 || drive_init() < 0 || servo_init() < 0) {
		printk("init failed\n");
		return 0;
	}

	/* Brakes both motors, then sets up the encoders */
	if (drive_init() < 0) {
		printk("drive_init failed\n");
		return 0;
	}

	if (picom_init() != 0) {
		printk("picom_init failed\n");
		return 1;
	}

	// TESTING ONLY - REMOVE
	drive_set_error(false);
	drive_command(TUNE_THROTTLE, false);
	picom_send("test\n\0");
	printk("test\n");

	uint8_t uart_packet[64];

	while (1) {
		if (picom_read(uart_packet)) {
			printk("packet recieved\n");
			uart_packet[63] = '\0';
			picom_send(uart_packet);
			printk("%s\n", uart_packet);
		}
		k_msleep(20);
	}

	return 0;
}
