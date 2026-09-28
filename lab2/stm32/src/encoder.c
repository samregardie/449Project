/*
 * Wheel encoders.
 *
 * The A/B pins (D6-D9) aren't on a timer's CH1/CH2 pair, so hardware encoder
 * mode isn't available: count edges in GPIO interrupts instead.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "encoder.h"

int encoder_init(void)
{
	/* TODO: configure enc-left/enc-right pins and edge interrupts */
	return 0;
}

int32_t encoder_count(enum side s)
{
	/* TODO */
	return 0;
}

int32_t encoder_velocity(enum side s)
{
	/* TODO */
	return 0;
}
