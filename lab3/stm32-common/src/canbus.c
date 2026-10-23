/*
 * Shared CAN layer: one MCP2515 per bus, every frame sent on both.
 *
 * RX: the filter callback runs in the MCP2515 driver thread, so it only
 * queues the frame. The dispatch thread records when each ID was last seen
 * per bus, drops the second copy by (ID, seq), and calls the handler.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

#include "canbus.h"

/* After this much silence, accept any seq (the sender restarted) */
#define DEDUP_RESET_MS 100

#define RX_QUEUE_LEN 32		/* ~4 frames per 20 ms per bus */
#define MAX_HANDLERS 8		/* IDs this node receives */
#define MAX_TX_IDS 4		/* IDs this node sends */

/* Wait for a free TX buffer: a few frame times (270 us each) of normal traffic */
#define TX_WAIT K_MSEC(2)

/* TODO (team): justify in the RTA */
#define DISPATCH_PRIO K_PRIO_PREEMPT(1)

static const struct device *const can[BUS_COUNT] = {
	DEVICE_DT_GET(DT_ALIAS(can_a)),
	DEVICE_DT_GET(DT_ALIAS(can_b)),
};

/* Test points */
#define USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec tp_tx[BUS_COUNT] = {
	GPIO_DT_SPEC_GET(USER_NODE, can_tx_a_gpios),
	GPIO_DT_SPEC_GET(USER_NODE, can_tx_b_gpios),
};

static const struct gpio_dt_spec tp_rx[BUS_COUNT] = {
	GPIO_DT_SPEC_GET(USER_NODE, can_rx_a_gpios),
	GPIO_DT_SPEC_GET(USER_NODE, can_rx_b_gpios),
};

struct handler {
	uint16_t id;
	canbus_handler_t fn;
	atomic_t last_seen[BUS_COUNT];	/* uptime ms, 0 = never */
	bool seen;			/* dedup state, dispatch thread only */
	uint8_t seq;
	uint32_t seq_ms;
};

static struct handler handlers[MAX_HANDLERS];
static int n_handlers;
static canbus_tap_t tap;

/* Set once canbus_init() has started both controllers; nothing is sent before */
static atomic_t ready;

static struct handler *find_handler(uint16_t id)
{
	for (int i = 0; i < n_handlers; i++) {
		if (handlers[i].id == id) {
			return &handlers[i];
		}
	}

	return NULL;
}

/* ---- RX ---- */

struct rx_msg {
	struct can_frame frame;
	uint8_t bus;
};

K_MSGQ_DEFINE(rx_queue, sizeof(struct rx_msg), RX_QUEUE_LEN, 4);

static void on_rx(const struct device *dev, struct can_frame *frame, void *user_data)
{
	struct rx_msg msg = { .frame = *frame, .bus = (uintptr_t)user_data };

	k_msgq_put(&rx_queue, &msg, K_NO_WAIT);
}

/* d = seq - last (mod 16): 0 is the duplicate, 8-15 is an old copy arriving late */
static bool is_new(struct handler *h, uint8_t seq, uint32_t now)
{
	uint8_t d = (seq - h->seq) & CAN_SEQ_MASK;

	if (h->seen && (d == 0 || d > 7) && now - h->seq_ms <= DEDUP_RESET_MS) {
		return false;
	}

	h->seen = true;
	h->seq = seq;
	h->seq_ms = now;
	return true;
}

static void dispatch(void *p1, void *p2, void *p3)
{
	struct rx_msg msg;

	while (1) {
		k_msgq_get(&rx_queue, &msg, K_FOREVER);
		gpio_pin_toggle_dt(&tp_rx[msg.bus]);

		const struct can_frame *f = &msg.frame;
		struct handler *h = find_handler(f->id);
		uint32_t now = k_uptime_get_32();

		if (h == NULL || f->dlc != 8) {
			continue;
		}

		atomic_set(&h->last_seen[msg.bus], now);

		if (is_new(h, f->data[0] & CAN_SEQ_MASK, now)) {
			if (tap != NULL) {
				tap(f, msg.bus);
			}
			h->fn(f);
		}
	}
}

K_THREAD_DEFINE(canbus_dispatch, 1024, dispatch, NULL, NULL, NULL, DISPATCH_PRIO, 0, 0);

int canbus_on(uint16_t id, canbus_handler_t handler)
{
	if (n_handlers == MAX_HANDLERS) {
		return -ENOMEM;
	}

	handlers[n_handlers++] = (struct handler){ .id = id, .fn = handler };
	return 0;
}

void canbus_tap(canbus_tap_t fn)
{
	tap = fn;
}

uint32_t canbus_ms_since(uint16_t id, enum canbus_bus bus)
{
	struct handler *h = find_handler(id);
	uint32_t seen = h ? atomic_get(&h->last_seen[bus]) : 0;

	return seen ? k_uptime_get_32() - seen : UINT32_MAX;
}

int canbus_init(void)
{
	for (int b = 0; b < BUS_COUNT; b++) {
		if (gpio_pin_configure_dt(&tp_tx[b], GPIO_OUTPUT_INACTIVE) != 0 ||
		    gpio_pin_configure_dt(&tp_rx[b], GPIO_OUTPUT_INACTIVE) != 0) {
			return -EIO;
		}

		if (!device_is_ready(can[b])) {
			return -ENODEV;
		}

		for (int i = 0; i < n_handlers; i++) {
			struct can_filter filter = { .id = handlers[i].id, .mask = CAN_STD_ID_MASK };
			int ret = can_add_rx_filter(can[b], on_rx, (void *)(uintptr_t)b, &filter);

			if (ret < 0) {
				return ret;
			}
		}

		int ret = can_start(can[b]);

		if (ret < 0) {
			return ret;
		}
	}

	atomic_set(&ready, 1);
	return 0;
}

/* ---- TX ---- */

/* Next seq per sent ID; id 0 = free slot (no catalog ID is 0) */
static struct {
	uint16_t id;
	uint8_t seq;
} tx_seqs[MAX_TX_IDS];

/* Keeps the seq bump and both queues together when several threads send */
K_MUTEX_DEFINE(tx_lock);

static uint8_t next_seq(uint16_t id)
{
	for (int i = 0; i < MAX_TX_IDS; i++) {
		if (tx_seqs[i].id == id || tx_seqs[i].id == 0) {
			tx_seqs[i].id = id;
			return tx_seqs[i].seq++ & CAN_SEQ_MASK;
		}
	}

	return 0;	/* table full: raise MAX_TX_IDS */
}

/* A callback makes can_send() return without waiting for the ACK */
static void tx_done(const struct device *dev, int error, void *user_data)
{
}

/*
 * -EAGAIN: no TX buffer freed up within TX_WAIT, so all 3 are stuck because
 * nobody on this bus ACKs. (TX_WAIT, not K_NO_WAIT, so a short burst such as
 * the relay forwarding several frames doesn't count as stuck.) Restarting
 * the controller drops them, so they aren't sent late.
 * -ENETDOWN: an earlier restart failed and left the controller stopped, so
 * try starting it again (can_stop() just returns -EALREADY then).
 */
static int send_on(int b, const struct can_frame *f)
{
	gpio_pin_toggle_dt(&tp_tx[b]);

	int ret = can_send(can[b], f, TX_WAIT, tx_done, NULL);

	if (ret == -EAGAIN || ret == -ENETDOWN) {
		can_stop(can[b]);
		can_start(can[b]);
		ret = can_send(can[b], f, K_NO_WAIT, tx_done, NULL);
	}

	return ret;
}

int canbus_send(uint16_t id, const uint8_t data[8])
{
	struct can_frame f = { .id = id, .dlc = 8 };

	if (!atomic_get(&ready)) {
		return -ENETDOWN;
	}

	memcpy(f.data, data, 8);

	k_mutex_lock(&tx_lock, K_FOREVER);

	f.data[0] = (data[0] & ~CAN_SEQ_MASK) | next_seq(id);

	int err_a = send_on(BUS_A, &f);
	int err_b = send_on(BUS_B, &f);

	k_mutex_unlock(&tx_lock);
	return err_a ? err_a : err_b;
}

int canbus_forward(enum canbus_bus bus, const struct can_frame *f)
{
	if (!atomic_get(&ready)) {
		return -ENETDOWN;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);

	int ret = send_on(bus, f);

	k_mutex_unlock(&tx_lock);
	return ret;
}
