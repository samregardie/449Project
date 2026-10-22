#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "blinker.h"
#include "canbus.h"
#include "ctrl.h"
#include "errorstate.h"
#include "servo.h"

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

	/* First, so the servo has left the error state */
	errorstate_ctrl_ok();
	servo_command(c.steer);
}

int main(void)
{
	if (servo_init() != 0 || errorstate_init(servo_set_error) != 0 ||
	    blinker_init() != 0 ||
	    canbus_on(CAN_ID_CTRL, on_ctrl) != 0 || canbus_init() != 0) {
		printk("init failed\n");
		return 1;
	}

	return 0;
}
