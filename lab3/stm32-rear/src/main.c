#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "blinker.h"
#include "canbus.h"
#include "ctrl.h"
#include "drive.h"
#include "errorstate.h"

#define BRAKE_THRESHOLD 10	/* brake above 10% counts as pressed */

static void on_ctrl(const struct can_frame *f)
{
	struct ctrl c;
	int ret = ctrl_parse(f, &c);

	if (ret == -ERANGE) {
		errorstate_bad_ctrl();
	}
	if (ret != 0) {
		return;
	}

	/* First, so the drive has left the error state */
	errorstate_ctrl_ok();
	drive_command(c.throttle, c.brake > BRAKE_THRESHOLD);
}

int main(void)
{
	if (drive_init() != 0 || errorstate_init(drive_set_error) != 0 ||
	    blinker_init() != 0 ||
	    canbus_on(CAN_ID_CTRL, on_ctrl) != 0 || canbus_init() != 0) {
		printk("init failed\n");
		return 1;
	}

	return 0;
}
