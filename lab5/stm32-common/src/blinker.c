#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

#include "blinker.h"
#include "canbus.h"
#include "errorstate.h"
#include "globaltime.h"

/* Half periods: a lamp is on while (global time / half period) is even */
#define TURN_HALF_US 500000	/* turn signal, 1 Hz */
#define ERROR_HALF_US 250000	/* hazards in the error state, 2 Hz */
#define FAILURE_HALF_US 125000	/* failure mode (no cockpit), 4 Hz */

/* Every lamp edge falls on a multiple of the shortest half period */
#define TICK_US FAILURE_HALF_US

static const struct gpio_dt_spec left = GPIO_DT_SPEC_GET(DT_NODELABEL(blink_left), gpios);
static const struct gpio_dt_spec right = GPIO_DT_SPEC_GET(DT_NODELABEL(blink_right), gpios);

static atomic_t state = ATOMIC_INIT(BLINK_OFF);	/* from the last BLINK */

/*
 * Timer ISR on every TICK_US of global time. With no HB_N1 on either bus
 * there is no cockpit and no BLINK, so we flash hazards ourselves (failure
 * mode); both STM32s do it from global time, so they stay in step.
 */
static void lamp_tick(struct k_timer *timer)
{
	bool lost = canbus_ms_since(CAN_ID_HB_N1, BUS_A) > ZONE_TIMEOUT_MS &&
		    canbus_ms_since(CAN_ID_HB_N1, BUS_B) > ZONE_TIMEOUT_MS;
	int s = lost ? BLINK_HAZARD : atomic_get(&state);
	int64_t half = lost ? FAILURE_HALF_US : s == BLINK_HAZARD ? ERROR_HALF_US : TURN_HALF_US;
	int64_t now = global_gettime();
	bool on = (now / half) % 2 == 0;

	if (lost) {
		atomic_set(&state, BLINK_OFF);	/* lamps off when the cockpit is back, until its BLINK */
	}

	gpio_pin_set_dt(&left, on && (s == BLINK_LEFT || s == BLINK_HAZARD));
	gpio_pin_set_dt(&right, on && (s == BLINK_RIGHT || s == BLINK_HAZARD));

	/* Next edge. Waking a few us early just runs this again right away. */
	k_timer_start(timer, K_USEC(TICK_US - now % TICK_US), K_NO_WAIT);
}

K_TIMER_DEFINE(lamp_timer, lamp_tick, NULL);

/* BLINK byte 1 = state. A new state shows now, not at the next edge */
static void on_blink(const struct can_frame *f)
{
	if (atomic_set(&state, f->data[1]) != f->data[1]) {
		k_timer_start(&lamp_timer, K_NO_WAIT, K_NO_WAIT);
	}
}

int blinker_init(void)
{
	if (gpio_pin_configure_dt(&left, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&right, GPIO_OUTPUT_INACTIVE) != 0) {
		return -EIO;
	}

	k_timer_start(&lamp_timer, K_NO_WAIT, K_NO_WAIT);
	return canbus_on(CAN_ID_BLINK, on_blink);
}
