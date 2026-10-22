/*
 * RELAY: the dispatch tap decides, the relay thread sends.
 *
 * A frame from zone S that arrived on bus X goes out on bus Y only when S's
 * heartbeat says it hears only X and the other zone's says it hears only Y.
 * Scenario (c), N1 off B and N3 off A, is the case this covers. With one
 * node off one bus, or a whole bus down, a common bus remains and nothing
 * is forwarded. Relaying starts when those heartbeats report the fault
 * (~60-80 ms after the pull) and stops when they report it gone.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "canbus.h"
#include "errorstate.h"
#include "relay.h"

/* Below dispatch (1), which feeds it; same as the MCP2515 RX threads (Kconfig default 2) */
#define RELAY_PRIO K_PRIO_PREEMPT(2)

#define RELAY_QUEUE_LEN 8

static const struct gpio_dt_spec relay_tx = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), relay_tx_gpios);

struct relay_msg {
	struct can_frame frame;
	uint8_t bus;		/* bus to send it on */
};

K_MSGQ_DEFINE(relay_queue, sizeof(struct relay_msg), RELAY_QUEUE_LEN, 4);

static bool only_on(int zone, enum canbus_bus bus)
{
	return errorstate_buses(zone) == BIT(bus);
}

/* Zone `from` reaches the other end zone only through us, from bus x to bus y */
static bool cut_off(int from, enum canbus_bus x, enum canbus_bus y)
{
	int to = from == 1 ? 3 : 1;

	return only_on(from, x) && only_on(to, y);
}

/* Dispatch thread: must not block */
static void on_frame(const struct can_frame *f, enum canbus_bus bus)
{
	enum canbus_bus other = bus == BUS_A ? BUS_B : BUS_A;
	bool forward;

	switch (f->id) {
	case CAN_ID_CTRL:
	case CAN_ID_BLINK:
	case CAN_ID_HB_N1:
		forward = cut_off(1, bus, other);
		break;
	case CAN_ID_HB_N3:
		forward = cut_off(3, bus, other);
		break;
	case CAN_ID_FAULT:	/* the payload doesn't say who sent it */
		forward = cut_off(1, bus, other) || cut_off(3, bus, other);
		break;
	default:
		forward = false;	/* FORCE and HB_N2 are ours, already on both buses */
	}

	if (forward) {
		struct relay_msg msg = { .frame = *f, .bus = other };

		k_msgq_put(&relay_queue, &msg, K_NO_WAIT);
	}
}

static void relay_loop(void *p1, void *p2, void *p3)
{
	struct relay_msg msg;

	while (1) {
		k_msgq_get(&relay_queue, &msg, K_FOREVER);
		gpio_pin_toggle_dt(&relay_tx);
		canbus_forward(msg.bus, &msg.frame);
	}
}

K_THREAD_DEFINE(relay_thread, 1024, relay_loop, NULL, NULL, NULL, RELAY_PRIO, 0, 0);

/* N2 doesn't act on FAULT; the handler only gets it through the filter */
static void ignore(const struct can_frame *f)
{
}

int relay_init(void)
{
	if (gpio_pin_configure_dt(&relay_tx, GPIO_OUTPUT_INACTIVE) != 0) {
		return -EIO;
	}

	canbus_tap(on_frame);
	return canbus_on(CAN_ID_FAULT, ignore);
}
