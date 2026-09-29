/*
 * Wheel encoders, x4 quadrature decoding.
 *
 * The A/B pins (D6-D9) aren't on a timer's CH1/CH2 pair, so hardware encoder
 * mode isn't available: count edges in GPIO interrupts instead. Every edge of
 * A and B interrupts, and a lookup table turns the old and new A/B state into
 * +1, -1 or 0 (no change, or an invalid jump where both pins changed).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

#include "encoder.h"

#define USER_NODE DT_PATH(zephyr_user)

/* Indexed by enum side */
static const struct gpio_dt_spec enc_a[] = {
	[SIDE_LEFT] = GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, enc_left_gpios, 0),
	[SIDE_RIGHT] = GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, enc_right_gpios, 0),
};

static const struct gpio_dt_spec enc_b[] = {
	[SIDE_LEFT] = GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, enc_left_gpios, 1),
	[SIDE_RIGHT] = GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, enc_right_gpios, 1),
};

/* Set true for a side that counts down when the car rolls forward */
static const bool reversed[] = {
	[SIDE_LEFT] = true,
	[SIDE_RIGHT] = false,
};

/*
 * Index = previous state << 2 | new state, where state = A << 1 | B.
 * Forward is 00 -> 01 -> 11 -> 10 -> 00.
 */
static const int8_t step[16] = {
	 0, +1, -1,  0,
	-1,  0,  0, +1,
	+1,  0,  0, -1,
	 0, -1, +1,  0,
};

/* Read by threads, so atomic */
static atomic_t count[2];

/* Only touched by that side's ISRs, which can't preempt each other */
static uint8_t prev[2];

/* A and B of one encoder are on different ports: one callback per pin */
static struct gpio_callback callbacks[4];	/* [side * 2 + 0] = A, + 1 = B */

static uint8_t read_state(enum side s)
{
	return (gpio_pin_get_dt(&enc_a[s]) << 1) | gpio_pin_get_dt(&enc_b[s]);
}

/* Runs in ISR context: two pin reads and an atomic add, nothing else */
static void on_edge(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	enum side s = (cb - callbacks) / 2;
	uint8_t cur = read_state(s);
	int8_t delta = step[(prev[s] << 2) | cur];

	prev[s] = cur;

	if (delta != 0) {
		atomic_add(&count[s], reversed[s] ? -delta : delta);
	}
}

int encoder_init(void)
{
	const struct gpio_dt_spec *pins[] = {
		&enc_a[SIDE_LEFT], &enc_b[SIDE_LEFT],
		&enc_a[SIDE_RIGHT], &enc_b[SIDE_RIGHT],
	};

	for (int i = 0; i < ARRAY_SIZE(pins); i++) {
		if (!gpio_is_ready_dt(pins[i])) {
			return -ENODEV;
		}

		int ret = gpio_pin_configure_dt(pins[i], GPIO_INPUT);

		if (ret < 0) {
			return ret;
		}
	}

	/* Start from the real pin levels so the first edge isn't miscounted */
	prev[SIDE_LEFT] = read_state(SIDE_LEFT);
	prev[SIDE_RIGHT] = read_state(SIDE_RIGHT);

	for (int i = 0; i < ARRAY_SIZE(pins); i++) {
		gpio_init_callback(&callbacks[i], on_edge, BIT(pins[i]->pin));

		int ret = gpio_add_callback_dt(pins[i], &callbacks[i]);

		if (ret < 0) {
			return ret;
		}

		ret = gpio_pin_interrupt_configure_dt(pins[i], GPIO_INT_EDGE_BOTH);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

int32_t encoder_count(enum side s)
{
	return atomic_get(&count[s]);
}

int32_t encoder_velocity(enum side s)
{
	/*
	 * TODO (team): either the change in encoder_count() over a fixed period,
	 * or the time between edges (k_cycle_get_32() in on_edge()). Think about
	 * low speed with each.
	 */
	return 0;
}
