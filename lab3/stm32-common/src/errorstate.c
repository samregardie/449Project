/*
 * HB thread, every 20 ms: DEG LEDs, error state, HB_TX, FAULT, watchdog feed.
 * CTRL and the test button also update the error state right away.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/atomic.h>

#include "canbus.h"
#include "errorstate.h"

#define HB_PERIOD_MS 20
#define DEBOUNCE_MS 50
#define DOUBLE_PRESS_MS 500

/* Fed every 20 ms; margin because the IWDG's LSI clock is imprecise */
#define WDT_TIMEOUT_MS 100

/* TODO (team): justify in the RTA */
#define HB_PRIO K_PRIO_PREEMPT(3)

/* FAULT reasons, same numbers as the Pi */
#define FAULT_TEST_BUTTON 2
#define FAULT_BAD_CTRL 3

/* Own fault bits */
#define OWN_TEST BIT(0)
#define OWN_BAD_CTRL BIT(1)

#define HB_ID(zone) (CAN_ID_HB_N1 + (zone) - 1)

#define USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec deg[BUS_COUNT] = {
	GPIO_DT_SPEC_GET(USER_NODE, deg_a_gpios),
	GPIO_DT_SPEC_GET(USER_NODE, deg_b_gpios),
};
static const struct gpio_dt_spec err_pin = GPIO_DT_SPEC_GET(USER_NODE, err_gpios);

/* Test button: the Nucleo's blue user button B1 (PC13) */
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;

static const struct device *const wdt = DEVICE_DT_GET(DT_NODELABEL(iwdg));
static int wdt_channel;

static atomic_t own_faults;
static atomic_t zone_fault[4];		/* by zone id, from their heartbeats */
static atomic_t zone_buses[4];		/* same, bus health bits */
static atomic_t last_ctrl_ms;		/* 0 = never */
static atomic_t last_press_ms;
static atomic_t pending_fault;		/* reason for the HB thread to send, 0 = none */

static errorstate_cb_t on_change;
static struct k_spinlock lock;
static bool in_error = true;

static bool heard(int zone, int bus)
{
	return canbus_ms_since(HB_ID(zone), bus) <= ZONE_TIMEOUT_MS;
}

/* Any context. The lock keeps ERR and on_change in step with in_error. */
static void update(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	uint32_t ctrl_ms = atomic_get(&last_ctrl_ms);
	bool error = atomic_get(&own_faults) || ctrl_ms == 0 ||
		     k_uptime_get_32() - ctrl_ms > ZONE_TIMEOUT_MS;

	for (int z = 1; z <= 3; z++) {
		if (z != NODE_ID && ((!heard(z, BUS_A) && !heard(z, BUS_B)) ||
				     atomic_get(&zone_fault[z]))) {
			error = true;
		}
	}

	if (error != in_error) {
		in_error = error;
		gpio_pin_set_dt(&err_pin, error);
		on_change(error);
	}

	k_spin_unlock(&lock, key);
}

static void hb_loop(void *p1, void *p2, void *p3)
{
	struct k_timer timer;

	k_timer_init(&timer, NULL, NULL);
	k_timer_start(&timer, K_MSEC(HB_PERIOD_MS), K_MSEC(HB_PERIOD_MS));

	while (1) {
		k_timer_status_sync(&timer);

		/* A bus is degraded when we hear no other zone on it */
		bool bus_ok[BUS_COUNT] = { false, false };

		for (int z = 1; z <= 3; z++) {
			for (int b = 0; b < BUS_COUNT; b++) {
				bus_ok[b] |= z != NODE_ID && heard(z, b);
			}
		}

		for (int b = 0; b < BUS_COUNT; b++) {
			gpio_pin_set_dt(&deg[b], !bus_ok[b]);
		}

		update();

		uint8_t hb[8] = { 0, atomic_get(&own_faults) != 0, bus_ok[BUS_A] | bus_ok[BUS_B] << 1 };

		canbus_send(HB_ID(NODE_ID), hb);

		uint8_t reason = atomic_clear(&pending_fault);

		if (reason) {
			uint8_t fault[8] = { 0, NODE_ID, reason };

			canbus_send(CAN_ID_FAULT, fault);
		}

		wdt_feed(wdt, wdt_channel);
	}
}

K_THREAD_DEFINE(hb_thread, 1024, hb_loop, NULL, NULL, NULL, HB_PRIO, 0, SYS_FOREVER_MS);

static void on_hb(const struct can_frame *f)
{
	int zone = f->id - CAN_ID_HB_N1 + 1;

	atomic_set(&zone_fault[zone], f->data[1]);
	atomic_set(&zone_buses[zone], f->data[2]);
}

/* One press enters, a second within 500 ms exits */
static void test_pressed(void)
{
	uint32_t now = k_uptime_get_32();
	bool double_press = (atomic_get(&own_faults) & OWN_TEST) &&
			    now - atomic_get(&last_press_ms) <= DOUBLE_PRESS_MS;

	if (double_press) {
		atomic_and(&own_faults, ~OWN_TEST);
	} else {
		atomic_or(&own_faults, OWN_TEST);
		atomic_set(&pending_fault, FAULT_TEST_BUTTON);
	}

	atomic_set(&last_press_ms, now);
	update();
}

/* ISR on both edges: a press counts only after 50 ms with no edges (bounce) */
static void on_button(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	static uint32_t last_edge_ms;
	uint32_t now = k_uptime_get_32();

	if (now - last_edge_ms > DEBOUNCE_MS && gpio_pin_get_dt(&button)) {
		test_pressed();
	}

	last_edge_ms = now;
}

int errorstate_init(errorstate_cb_t cb)
{
	on_change = cb;

	if (gpio_pin_configure_dt(&deg[BUS_A], GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&deg[BUS_B], GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&err_pin, GPIO_OUTPUT_ACTIVE) != 0 ||
	    gpio_pin_configure_dt(&button, GPIO_INPUT) != 0) {
		return -EIO;
	}

	struct wdt_timeout_cfg wdt_cfg = { .window.max = WDT_TIMEOUT_MS, .flags = WDT_FLAG_RESET_SOC };

	wdt_channel = wdt_install_timeout(wdt, &wdt_cfg);
	if (wdt_channel < 0 || wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG) != 0) {
		return -EIO;
	}

	gpio_init_callback(&button_cb, on_button, BIT(button.pin));
	if (gpio_add_callback_dt(&button, &button_cb) != 0 ||
	    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH) != 0) {
		return -EIO;
	}

	for (int z = 1; z <= 3; z++) {
		if (z != NODE_ID) {
			canbus_on(HB_ID(z), on_hb);
		}
	}

	k_thread_start(hb_thread);
	return 0;
}

void errorstate_ctrl_ok(void)
{
	atomic_set(&last_ctrl_ms, k_uptime_get_32());
	atomic_and(&own_faults, ~OWN_BAD_CTRL);
	update();
}

void errorstate_bad_ctrl(void)
{
	atomic_or(&own_faults, OWN_BAD_CTRL);
	atomic_set(&pending_fault, FAULT_BAD_CTRL);
	update();
}

uint8_t errorstate_buses(int zone)
{
	if (!heard(zone, BUS_A) && !heard(zone, BUS_B)) {
		return 0;
	}

	return atomic_get(&zone_buses[zone]);
}
