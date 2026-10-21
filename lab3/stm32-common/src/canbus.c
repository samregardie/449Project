/*
 * Shared CAN layer: one MCP2515 per bus, every frame sent on both.
 *
 * RX: the filter callback runs in the MCP2515 driver thread with the driver's
 * mutex held, so it only queues the frame with its bus. The dispatch thread
 * then takes each copy in arrival order: toggle CAN_RX_x, record last-seen
 * per bus (for HB_MON), drop duplicates and late copies, call the handler.
 * Only the dispatch thread touches the dedup state, so it needs no lock.
 *
 * TX: canbus_send() bumps that ID's seq once and queues on A, then B.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

#include "canbus.h"

/*
 * After this much silence on a stream, accept any seq (sender rebooted, or a
 * bus came back). Longer than the A/B gap between copies, shorter than the
 * 7 periods (140 ms) the seq window covers.
 */
#define DEDUP_RESET_MS 100

/* ~4 frames per 20 ms per bus; 32 covers several periods */
#define RX_QUEUE_LEN 32

#define MAX_HANDLERS 8		/* IDs this node receives */
#define MAX_TX_IDS 4		/* IDs this node sends */
#define MAX_SENDERS 4		/* index 0 for single-sender IDs, zone 1-3 for FAULT */

/* TODO (team): justify in the RTA. Above every handler's consumer */
#define DISPATCH_PRIO K_PRIO_PREEMPT(1)

static const struct device *const can[BUS_COUNT] = {
	DEVICE_DT_GET(DT_ALIAS(can_a)),
	DEVICE_DT_GET(DT_ALIAS(can_b)),
};

/* Test points, skipped until the overlay defines them */
#define USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec tp_tx[BUS_COUNT] = {
	GPIO_DT_SPEC_GET_OR(USER_NODE, can_tx_a_gpios, {0}),
	GPIO_DT_SPEC_GET_OR(USER_NODE, can_tx_b_gpios, {0}),
};

static const struct gpio_dt_spec tp_rx[BUS_COUNT] = {
	GPIO_DT_SPEC_GET_OR(USER_NODE, can_rx_a_gpios, {0}),
	GPIO_DT_SPEC_GET_OR(USER_NODE, can_rx_b_gpios, {0}),
};

static void toggle(const struct gpio_dt_spec *tp)
{
	if (tp->port != NULL) {
		gpio_pin_toggle_dt(tp);
	}
}

/* One stream = one sender's frames for one ID */
struct stream {
	bool seen;
	uint8_t seq;
	uint32_t ms;		/* when seq was accepted */
};

/* The table is fixed once canbus_init() runs */
struct handler {
	uint16_t id;
	canbus_handler_t fn;
	atomic_t last_seen[BUS_COUNT];		/* written by dispatch, read by HB_MON */
	struct stream streams[MAX_SENDERS];	/* dispatch only */
};

static struct handler handlers[MAX_HANDLERS];
static int n_handlers;

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

/* Non-zero means the queue is too short or dispatch is starved */
static atomic_t rx_dropped;

/* Runs in the MCP2515 driver thread: no blocking, no sending */
static void on_rx(const struct device *dev, struct can_frame *frame, void *user_data)
{
	struct rx_msg msg = { .frame = *frame, .bus = (uintptr_t)user_data };

	if (k_msgq_put(&rx_queue, &msg, K_NO_WAIT) != 0) {
		atomic_inc(&rx_dropped);
	}
}

/*
 * d = (seq - last) mod 16: 0 = duplicate, 1-7 = newer, 8-15 = late copy.
 * The first frame, or one after DEDUP_RESET_MS of silence, is always new.
 */
static bool is_new(struct stream *s, uint8_t seq, uint32_t now)
{
	uint8_t d = (seq - s->seq) & CAN_SEQ_MASK;

	if (s->seen && (d == 0 || d > 7) && now - s->ms <= DEDUP_RESET_MS) {
		return false;
	}

	*s = (struct stream){ .seen = true, .seq = seq, .ms = now };
	return true;
}

static void dispatch(void *p1, void *p2, void *p3)
{
	struct rx_msg msg;

	while (1) {
		k_msgq_get(&rx_queue, &msg, K_FOREVER);
		toggle(&tp_rx[msg.bus]);

		const struct can_frame *f = &msg.frame;
		struct handler *h = find_handler(f->id);
		uint8_t sender = (f->id == CAN_ID_FAULT) ? f->data[CAN_FAULT_ZONE_BYTE] : 0;
		uint32_t now = k_uptime_get_32();

		/* Every catalog frame is DLC 8; a bad zone byte is malformed */
		if (h == NULL || f->dlc != 8 || sender >= MAX_SENDERS) {
			continue;
		}

		atomic_set(&h->last_seen[msg.bus], now);

		if (is_new(&h->streams[sender], f->data[0] & CAN_SEQ_MASK, now)) {
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

uint32_t canbus_ms_since(uint16_t id, enum canbus_bus bus)
{
	struct handler *h = find_handler(id);

	if (h == NULL) {
		return UINT32_MAX;
	}

	return k_uptime_get_32() - (uint32_t)atomic_get(&h->last_seen[bus]);
}

int canbus_init(void)
{
	for (int b = 0; b < BUS_COUNT; b++) {
		if (tp_tx[b].port != NULL) {
			gpio_pin_configure_dt(&tp_tx[b], GPIO_OUTPUT_INACTIVE);
		}
		if (tp_rx[b].port != NULL) {
			gpio_pin_configure_dt(&tp_rx[b], GPIO_OUTPUT_INACTIVE);
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

	return 0;
}

/* ---- TX ---- */

/* Next seq per sent ID */
static struct {
	uint16_t id;
	uint8_t seq;
} tx_seqs[MAX_TX_IDS];
static int n_tx_ids;

/* Keeps the seq bump and both queues together when several threads send */
K_MUTEX_DEFINE(tx_lock);

/* Passing a callback makes can_send() return without waiting for an ACK */
static void tx_done(const struct device *dev, int error, void *user_data)
{
}

/*
 * -EAGAIN means all 3 TX buffers are stuck retrying: nobody on this bus is
 * ACKing. Restarting the controller drops them, so frames from before the
 * outage aren't sent when the bus comes back.
 */
static int send_on(int b, const struct can_frame *f)
{
	toggle(&tp_tx[b]);

	int ret = can_send(can[b], f, K_NO_WAIT, tx_done, NULL);

	if (ret == -EAGAIN) {
		can_stop(can[b]);
		can_start(can[b]);
		ret = can_send(can[b], f, K_NO_WAIT, tx_done, NULL);
	}

	return ret;
}

int canbus_send(uint16_t id, const uint8_t data[8])
{
	struct can_frame f = { .id = id, .dlc = 8 };
	int i;

	memcpy(f.data, data, 8);

	k_mutex_lock(&tx_lock, K_FOREVER);

	for (i = 0; i < n_tx_ids && tx_seqs[i].id != id; i++) {
	}

	if (i == MAX_TX_IDS) {
		k_mutex_unlock(&tx_lock);
		return -ENOMEM;
	}

	if (i == n_tx_ids) {
		tx_seqs[n_tx_ids++].id = id;	/* first send of this ID, seq 0 */
	}

	f.data[0] = (data[0] & ~CAN_SEQ_MASK) | tx_seqs[i].seq;
	tx_seqs[i].seq = (tx_seqs[i].seq + 1) & CAN_SEQ_MASK;

	int err_a = send_on(BUS_A, &f);
	int err_b = send_on(BUS_B, &f);

	k_mutex_unlock(&tx_lock);
	return err_a ? err_a : err_b;
}
