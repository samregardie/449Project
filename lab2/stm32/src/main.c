
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
	picom_send("test\n");

	while (1) {
		struct drive_status s = drive_get_status();

		/* printk has no %f, so x10 */
		printk("%d,%d,%d\n", (int)(s.target * 10), (int)(s.velocity * 10), s.duty);
		k_msleep(20);
	}

	return 0;
}
