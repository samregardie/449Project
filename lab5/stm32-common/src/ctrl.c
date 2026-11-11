#include <errno.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#include "ctrl.h"

int ctrl_parse(const struct can_frame *f, struct ctrl *c)
{
	/* CRC-8, polynomial 0x07, start 0: same as the Pi */
	if (crc8_ccitt(0, f->data, 6) != f->data[6]) {
		return -EBADMSG;
	}

	c->steer = (int16_t)sys_get_le16(&f->data[1]);
	c->throttle = f->data[3];
	c->brake = f->data[4];
	c->buttons = f->data[5];

	if (c->steer < -100 || c->steer > 100 || c->throttle > 100 || c->brake > 100) {
		return -ERANGE;
	}

	return 0;
}
