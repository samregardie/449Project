#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "servo.h"

int main(void)
{
	if (servo_init() != 0) {
		printk("init failed\n");
		return 1;
	}

	return 0;
}
