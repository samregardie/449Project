#ifndef CTRL_H
#define CTRL_H

#include <stdint.h>

#include <zephyr/drivers/can.h>

/* CTRL payload, as lab3/pi/proxy_receiver/receiver.c builds it */
struct ctrl {
	int16_t steer;		/* -100..100 */
	uint8_t throttle;	/* 0..100 */
	uint8_t brake;		/* 0..100 */
	uint8_t buttons;
};

/* 0 = valid, -EBADMSG = bad CRC (drop it), -ERANGE = out of range (error state) */
int ctrl_parse(const struct can_frame *f, struct ctrl *c);

#endif
