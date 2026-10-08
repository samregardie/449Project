#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "blinker.h"
#include "drive.h"
#include "current.h"
#include "errorstate.h"
#include "servo.h"
#include "picom.h"
#include "status.h"

int main(void)
{
	if (current_sense_init() != 0 || 
		drive_init() != 0 || 
		blinker_init() != 0 || 
		servo_init() != 0 ||
		picom_init() != 0 ||
		status_init() != 0) {
		printk("init failed\n");
		return 1;
	}

	return 0;
}
