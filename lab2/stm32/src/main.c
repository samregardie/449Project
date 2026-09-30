
#include "blinker.h"
#include "drive.h"
#include "errorstate.h"
#include "servo.h"



int main(void)
{
	/* All start in the error state: hazards, braked, servo centred */
	if (blinker_init() < 0 || drive_init() < 0 || servo_init() < 0) {
		printk("init failed\n");
		return 0;
	}


	return 0;
}
