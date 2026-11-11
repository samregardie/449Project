#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "blinker.h"
#include "canbus.h"
#include "errorstate.h"

/* BLINK byte 1, same as the Pi */
enum { BLINK_OFF, BLINK_LEFT, BLINK_RIGHT, BLINK_HAZARD };

/* Own hazards with no cockpit: 1 Hz, same as the cockpit's */
#define LOCAL_HALF_PERIOD_MS 500

static const struct gpio_dt_spec left = GPIO_DT_SPEC_GET(DT_NODELABEL(blink_left), gpios);
static const struct gpio_dt_spec right = GPIO_DT_SPEC_GET(DT_NODELABEL(blink_right), gpios);

/* Shared by the dispatch thread and the timer ISR */
static struct k_spinlock lock;
static bool local;		/* cockpit lost: we run our own hazards */
static bool local_on;

static void set_lamps(bool l, bool r)
{
	gpio_pin_set_dt(&left, l);
	gpio_pin_set_dt(&right, r);
}

/* Byte 1 = state, byte 2 = phase (1 = lamps on) */
static void on_blink(const struct can_frame *f)
{
	uint8_t state = f->data[1];
	bool on = f->data[2];
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (!local) {
		set_lamps(on && (state == BLINK_LEFT || state == BLINK_HAZARD),
			  on && (state == BLINK_RIGHT || state == BLINK_HAZARD));
	}

	k_spin_unlock(&lock, key);
}

/*
 * Timer ISR. No HB_N1 on either bus means no BLINK either, so we flash
 * hazards ourselves (nothing to stay in sync with). When the cockpit is
 * back, lamps off until its next BLINK.
 */
static void local_tick(struct k_timer *timer)
{
	bool lost = canbus_ms_since(CAN_ID_HB_N1, BUS_A) > ZONE_TIMEOUT_MS &&
		    canbus_ms_since(CAN_ID_HB_N1, BUS_B) > ZONE_TIMEOUT_MS;
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (lost) {
		local = true;
		local_on = !local_on;
		set_lamps(local_on, local_on);
	} else if (local) {
		local = false;
		set_lamps(false, false);
	}

	k_spin_unlock(&lock, key);
}

K_TIMER_DEFINE(local_timer, local_tick, NULL);

int blinker_init(void)
{
	if (gpio_pin_configure_dt(&left, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&right, GPIO_OUTPUT_INACTIVE) != 0) {
		return -EIO;
	}

	k_timer_start(&local_timer, K_MSEC(LOCAL_HALF_PERIOD_MS), K_MSEC(LOCAL_HALF_PERIOD_MS));
	return canbus_on(CAN_ID_BLINK, on_blink);
}
