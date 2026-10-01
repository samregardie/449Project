#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "blinker.h"
#include "drive.h"
#include "current.h"
#include "errorstate.h"
#include "servo.h"
#include "picom.h"

int main(void)
{
	if (current_sense_init() != 0 || 
		drive_init() != 0 || 
		blinker_init() != 0 || 
		servo_init() != 0 ||
		picom_init() != 0) {
		printk("init failed\n");
		return 1;
	}

	// TESTING ONLY - REMOVE
	picom_send("test\n\x7F");
	printk("test\n");

	uint8_t uart_packet[64];

	while (1) {
		if (picom_read(uart_packet)) {
			printk("packet recieved\n");
			uart_packet[63] = 0x7F;
			picom_send(uart_packet);
			printk("%s\n", uart_packet);
		}
		k_msleep(20);
	}

	return 0;
}
