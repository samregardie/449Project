/*
 * Turn signals and hazards.
 *
 * A periodic k_timer fires every half period (500 ms, or 250 ms for hazards)
 * and toggles whichever LEDs the current state uses. Zephyr schedules each
 * expiry a fixed step after the previous *scheduled* one, so it doesn't drift.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "blinker.h"

enum blinker_state {
	OFF,
	LEFT,
	LEFT_TURNING,	/* left on, wheel has passed the left threshold */
	RIGHT,
	RIGHT_TURNING,	/* right on, wheel has passed the right threshold */
	HAZARD,
};

static const struct gpio_dt_spec fl = GPIO_DT_SPEC_GET(DT_NODELABEL(blink_fl), gpios);
static const struct gpio_dt_spec fr = GPIO_DT_SPEC_GET(DT_NODELABEL(blink_fr), gpios);
static const struct gpio_dt_spec rl = GPIO_DT_SPEC_GET(DT_NODELABEL(blink_rl), gpios);
static const struct gpio_dt_spec rr = GPIO_DT_SPEC_GET(DT_NODELABEL(blink_rr), gpios);

/* Shared with the timer callback (ISR), so always accessed under the lock */
static struct k_spinlock lock;
static enum blinker_state state = OFF;
static bool led_on;

static bool uses_left(enum blinker_state s)
{
	return s == LEFT || s == LEFT_TURNING || s == HAZARD;
}

static bool uses_right(enum blinker_state s)
{
	return s == RIGHT || s == RIGHT_TURNING || s == HAZARD;
}

static void write_leds(void)
{
	bool left = led_on && uses_left(state);
	bool right = led_on && uses_right(state);

	gpio_pin_set_dt(&fl, left);
	gpio_pin_set_dt(&rl, left);
	gpio_pin_set_dt(&fr, right);
	gpio_pin_set_dt(&rr, right);
}

/* Runs in ISR context: only GPIO writes, no blocking or logging */
static void blink(struct k_timer *timer)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	led_on = !led_on;
	write_leds();

	k_spin_unlock(&lock, key);
}

K_TIMER_DEFINE(blink_timer, blink, NULL);

/* Caller holds the lock */
static void set_state(enum blinker_state new_state)
{
	bool same_leds = uses_left(new_state) == uses_left(state) &&
			 uses_right(new_state) == uses_right(state);

	state = new_state;

	/* e.g. LEFT -> LEFT_TURNING: keep blinking without a restart */
	if (same_leds) {
		return;
	}

	led_on = false;
	write_leds();

	if (state == OFF) {
		k_timer_stop(&blink_timer);
	} else {
		/* First toggle (LEDs on) happens right away */
		k_timeout_t half_period = (state == HAZARD) ? K_MSEC(250) : K_MSEC(500);

		k_timer_start(&blink_timer, K_NO_WAIT, half_period);
	}
}

int blinker_init(void)
{
	const struct gpio_dt_spec *leds[] = { &fl, &fr, &rl, &rr };

	for (int i = 0; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(leds[i])) {
			return -ENODEV;
		}

		int ret = gpio_pin_configure_dt(leds[i], GPIO_OUTPUT_INACTIVE);

		if (ret < 0) {
			return ret;
		}
	}

	/* Power-up starts in the error state */
	blinker_set_error(true);

	return 0;
}

void blinker_left_pressed(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Left cancels right; pressing left again does nothing; ignored in hazard */
	if (state == OFF) {
		set_state(LEFT);
	} else if (state == RIGHT || state == RIGHT_TURNING) {
		set_state(OFF);
	}

	k_spin_unlock(&lock, key);
}

void blinker_right_pressed(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (state == OFF) {
		set_state(RIGHT);
	} else if (state == LEFT || state == LEFT_TURNING) {
		set_state(OFF);
	}

	k_spin_unlock(&lock, key);
}

void blinker_steering(int32_t steering)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (state == LEFT && steering < -BLINKER_TURN_THRESHOLD) {
		set_state(LEFT_TURNING);
	} else if (state == LEFT_TURNING && steering > -BLINKER_TURN_THRESHOLD) {
		set_state(OFF);
	} else if (state == RIGHT && steering > BLINKER_TURN_THRESHOLD) {
		set_state(RIGHT_TURNING);
	} else if (state == RIGHT_TURNING && steering < BLINKER_TURN_THRESHOLD) {
		set_state(OFF);
	}

	k_spin_unlock(&lock, key);
}

void blinker_set_error(bool error)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (error) {
		set_state(HAZARD);
	} else if (state == HAZARD) {
		set_state(OFF);
	}

	k_spin_unlock(&lock, key);
}
