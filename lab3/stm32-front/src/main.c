#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "canbus.h"
#include "servo.h"

/* TEMP: CAN layer test. Prints each CTRL once, however many copies arrive */
static void on_ctrl(const struct can_frame *f)
{
	printk("CTRL seq %u\n", f->data[0] & CAN_SEQ_MASK);
}

int main(void)
{
	if (servo_init() != 0) {
		printk("init failed\n");
		return 1;
	}

	canbus_on(CAN_ID_CTRL, on_ctrl);

	int ret = canbus_init();

	if (ret != 0) {
		printk("canbus init failed (%d)\n", ret);
		return 1;
	}

	return 0;
}
