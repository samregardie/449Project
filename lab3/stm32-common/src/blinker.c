#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "blinker.h"
#include "canbus.h"

/* BLINK byte 1, same as the Pi */
enum { BLINK_OFF, BLINK_LEFT, BLINK_RIGHT, BLINK_HAZARD };

static const struct gpio_dt_spec left = GPIO_DT_SPEC_GET(DT_NODELABEL(blink_left), gpios);
static const struct gpio_dt_spec right = GPIO_DT_SPEC_GET(DT_NODELABEL(blink_right), gpios);

/* Byte 1 = state, byte 2 = phase (1 = lamps on) */
static void on_blink(const struct can_frame *f)
{
	uint8_t state = f->data[1];
	bool on = f->data[2];

	gpio_pin_set_dt(&left, on && (state == BLINK_LEFT || state == BLINK_HAZARD));
	gpio_pin_set_dt(&right, on && (state == BLINK_RIGHT || state == BLINK_HAZARD));
}

int blinker_init(void)
{
	if (gpio_pin_configure_dt(&left, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&right, GPIO_OUTPUT_INACTIVE) != 0) {
		return -EIO;
	}

	return canbus_on(CAN_ID_BLINK, on_blink);
}
