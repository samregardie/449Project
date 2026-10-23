/*
 * RELAY: the dispatch tap decides, the relay thread sends.
 *
 * A node relays when it sees, from its own receive times, that the other two
 * zones share no bus: no HB from one on bus A and none from the other on bus
 * B for RELAY_DETECT_MS. N2 covers N1/N3 (scenario c), N3 covers N1/N2 and
 * the Pi covers N2/N3. With one node off one bus, or a whole bus down, a
 * common bus remains and nothing is forwarded. RELAY_DETECT_MS is below
 * ZONE_TIMEOUT_MS, so relayed heartbeats arrive before a zone is declared
 * failed. Only new frames reach the tap, so a frame is relayed at most once
 * per node and can't loop.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "canbus.h"
#include "relay.h"

/* Below dispatch (1), which feeds it. The MCP2515 RX threads are cooperative, above both */
#define RELAY_PRIO K_PRIO_PREEMPT(2)

#define RELAY_QUEUE_LEN 8
#define RELAY_DETECT_MS 30	/* one missed heartbeat, plus margin */

/* The other two zones */
#define ZONE_X (NODE_ID == 1 ? 2 : 1)
#define ZONE_Y (NODE_ID == 3 ? 2 : 3)
#define HB_ID(zone) (CAN_ID_HB_N1 + (zone) - 1)

static const struct gpio_dt_spec relay_tx = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), relay_tx_gpios);

struct relay_msg {
	struct can_frame frame;
	uint8_t bus;		/* bus to send it on */
};

K_MSGQ_DEFINE(relay_queue, sizeof(struct relay_msg), RELAY_QUEUE_LEN, 4);

static bool silent(uint16_t id, enum canbus_bus bus)
{
	return canbus_ms_since(id, bus) > RELAY_DETECT_MS;
}

/* The other two zones have no bus in common */
static bool split(void)
{
	return (silent(HB_ID(ZONE_X), BUS_B) && silent(HB_ID(ZONE_Y), BUS_A)) ||
	       (silent(HB_ID(ZONE_X), BUS_A) && silent(HB_ID(ZONE_Y), BUS_B));
}

/*
 * Dispatch thread: must not block. Every frame we receive is from one of the
 * other two zones (our own IDs have no handler), so when split, all of it
 * goes across.
 */
static void on_frame(const struct can_frame *f, enum canbus_bus bus)
{
	if (split()) {
		struct relay_msg msg = { .frame = *f, .bus = bus == BUS_A ? BUS_B : BUS_A };

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

/* For IDs we only relay: the handler gets them through the filter */
static void ignore(const struct can_frame *f)
{
}

int relay_init(void)
{
	if (gpio_pin_configure_dt(&relay_tx, GPIO_OUTPUT_INACTIVE) != 0) {
		return -EIO;
	}

	canbus_tap(on_frame);

	/* FORCE is N2's own; every other ID is already handled elsewhere */
	if (NODE_ID != 2 && canbus_on(CAN_ID_FORCE, ignore) != 0) {
		return -ENOMEM;
	}

	return canbus_on(CAN_ID_FAULT, ignore);
}
