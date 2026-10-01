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

	while (1) {
		printk("I (+/-100 = +/-%d mA): L=%d R=%d S=%d  err=0x%x\n",
		       CURRENT_FULL_SCALE_MA,
		       status_scale_current(get_current(MOTOR_LEFT)),
		       status_scale_current(get_current(MOTOR_RIGHT)),
		       status_scale_current(get_current(SERVO)),
		       errorstate_causes());
		k_msleep(500);
	}

	return 0;
}
