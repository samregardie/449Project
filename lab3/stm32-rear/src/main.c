#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "drive.h"

int main(void)
{
	if (drive_init() != 0) {
		printk("init failed\n");
		return 1;
	}

	return 0;
}
