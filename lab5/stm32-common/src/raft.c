/*
 * The STM32 side of the Raft agent: CAN handlers, a thread and printing.
 * The Raft rules themselves are raftnode.c and raftlog.c, shared with the Pi.
 *
 * Handlers run in the CAN dispatch thread. They only hand the frame to the
 * agent and wake the raft thread, which does all the sending and printing:
 * a send can block for a few ms when a bus is down, and the dispatch thread
 * also timestamps REF and runs CTRL.
 *
 * Raft client: link and node changes need no message of our own. Our HB
 * already carries our view of our own buses, and the leader logs the
 * changes. The client only sends EVENTs (one per test-button press).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "canbus.h"
#include "raft.h"
#include "raftnode.h"
#include "relay.h"

/* Below the CAN, control and HB threads (1-3) */
#define RAFT_PRIO K_PRIO_PREEMPT(4)

#define HB_TIMEOUT_MS (RAFT_MISSED_HEARTBEATS * RAFT_HEARTBEAT_MS)

/* The agent and the log (neither is thread-safe) */
K_MUTEX_DEFINE(raft_lock);

/* Wakes the raft thread: a Raft frame or a button press */
K_SEM_DEFINE(raft_sem, 0, 1);

static atomic_t presses;	/* test-button presses, from an ISR */

static void on_append(const struct can_frame *f)
{
	k_mutex_lock(&raft_lock, K_FOREVER);
	raftnode_on_append(f->id - CAN_ID_APPEND(0), f->data);
	k_mutex_unlock(&raft_lock);
	k_sem_give(&raft_sem);
}

static void on_ack(const struct can_frame *f)
{
	k_mutex_lock(&raft_lock, K_FOREVER);
	raftnode_on_ack(f->id - CAN_ID_APPEND_ACK(0), f->data);
	k_mutex_unlock(&raft_lock);
	k_sem_give(&raft_sem);
}

static void on_client(const struct can_frame *f)
{
	k_mutex_lock(&raft_lock, K_FOREVER);
	raftnode_on_client(f->id - CAN_ID_CLIENT(0), f->data);
	k_mutex_unlock(&raft_lock);
	k_sem_give(&raft_sem);
}

void raft_event(void)
{
	atomic_inc(&presses);
	k_sem_give(&raft_sem);
}

/* ---- Raft thread ---- */

/* Nodes whose HB we heard on either bus lately */
static void heard_nodes(bool alive[RAFT_N + 1])
{
	for (int n = 1; n <= RAFT_N; n++) {
		uint16_t hb = CAN_ID_HB_N1 + n - 1;

		alive[n] = n == NODE_ID || canbus_ms_since(hb, BUS_A) <= HB_TIMEOUT_MS ||
			   canbus_ms_since(hb, BUS_B) <= HB_TIMEOUT_MS;
	}
}

/* Apply newly committed entries and print them, the same lines on every node */
static void print_commits(void)
{
	bool any = false;

	while (1) {
		k_mutex_lock(&raft_lock, K_FOREVER);
		uint16_t i = log_apply_next();
		uint8_t t = log_term(i), e = log_entry(i);
		k_mutex_unlock(&raft_lock);

		if (i == 0) {
			break;
		}
		printk("[N%d] COMMIT idx=%d term=%d %s node=%d (0x%02x)\n", NODE_ID, i, t,
		       entry_name(e), ENTRY_NODE(e), e);
		any = true;
	}

	if (any) {
		char buf[96];

		k_mutex_lock(&raft_lock, K_FOREVER);
		struct network net = get_network();
		k_mutex_unlock(&raft_lock);

		network_format(&net, buf, sizeof(buf));
		printk("[N%d] NET %s\n", NODE_ID, buf);

		/* Routing (Part 5): rebuilt from the network every time it changes */
		relay_set_network(network_relay(&net, NODE_ID));
	}
}

static void raft_loop(void *p1, void *p2, void *p3)
{
	int pressed = 0;	/* presses handed to the agent */

	while (1) {
		/* Woken by a frame or a press; the timeout drives retries */
		k_sem_take(&raft_sem, K_MSEC(RAFT_HEARTBEAT_MS));

		struct raft_frame out[RAFT_FRAMES_MAX];
		static char notes[384];
		bool alive[RAFT_N + 1];
		int presses_now = atomic_get(&presses);

		heard_nodes(alive);

		k_mutex_lock(&raft_lock, K_FOREVER);
		for (; pressed < presses_now; pressed++) {
			raftnode_event();
		}
		int count = raftnode_poll(k_uptime_get(), alive, out);
		raftnode_notes(notes, sizeof(notes));
		k_mutex_unlock(&raft_lock);

		for (int i = 0; i < count; i++) {
			canbus_send(out[i].id, out[i].data);
		}
		printk("%s", notes);
		print_commits();
	}
}

K_THREAD_DEFINE(raft_thread, 2048, raft_loop, NULL, NULL, NULL, RAFT_PRIO, 0, SYS_FOREVER_MS);

int raft_init(void)
{
	raftnode_init(NODE_ID);

	/* The other two nodes' Raft frames. The other STM32's also reach the Pi
	 * through us when we relay (canbus receives only IDs with a handler) */
	for (int n = 1; n <= RAFT_N; n++) {
		if (n != NODE_ID && (canbus_on(CAN_ID_APPEND(n), on_append) != 0 ||
				     canbus_on(CAN_ID_APPEND_ACK(n), on_ack) != 0 ||
				     canbus_on(CAN_ID_CLIENT(n), on_client) != 0)) {
			return -ENOMEM;
		}
	}

	k_thread_start(raft_thread);
	return 0;
}
