/*
 * Raft agent on the STM32s: the follower side of log replication, and the
 * Raft client. The log and state machine are raftlog.c, shared with the Pi.
 *
 * Handlers run in the CAN dispatch thread. They only update state and wake
 * the raft thread, which does all the sending: a send can block for a few ms
 * when a bus is down, and the dispatch thread also timestamps REF.
 *
 * Raft client: link and node changes need no message of our own. Our HB
 * already carries our view of our own buses, and the leader logs the
 * changes. The client only sends EVENTs (one per test-button press):
 * CLIENT REQUEST with a request number, repeated every 100 ms until the
 * leader's CLIENT_OK, so retries never add a second entry (rule 5).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "canbus.h"
#include "raft.h"
#include "raftlog.h"
#include "relay.h"

/* Below the CAN, control and HB threads (1-3) */
#define RAFT_PRIO K_PRIO_PREEMPT(4)

#define CLIENT_RETRY_MS 100

/* The other STM32 */
#define OTHER_STM32 (NODE_ID == NODE_STEERING ? NODE_DRIVE : NODE_STEERING)

/* Everything below that isn't atomic: the log (raftlog.c is not thread-safe) */
K_MUTEX_DEFINE(raft_lock);

/* Wakes the raft thread: an APPEND, a CLIENT frame or a button press */
K_SEM_DEFINE(raft_sem, 0, 1);

/* With a default leader every node boots in term 1 with it (Part 2) */
static uint8_t term = RAFT_DEFAULT_LEADER != RAFT_NO_LEADER ? 1 : 0;
static uint8_t leader = RAFT_DEFAULT_LEADER;	/* who we think leads */

/*
 * The reply to the latest APPEND. If two APPENDs arrive before the raft
 * thread runs, only the newer reply goes out: it describes our log as it is
 * now, which is all the leader needs.
 */
static struct {
	bool pending;
	bool ok;
	uint16_t match;
} reply;

/* A REQUEST addressed to us, though we don't lead: tell its client who does */
static struct {
	bool pending;
	uint8_t client;
	uint8_t req;
} redirect;

/* EVENT client. Request numbers run 1..255, then wrap */
static atomic_t events_wanted;	/* button presses, from an ISR */
static atomic_t events_done;	/* OKed by the leader */
static atomic_t ok_req;		/* request number of the last OK for us */
static atomic_t retry_now;	/* a REDIRECT told us the leader: resend now */

static uint8_t req_number(int done)
{
	return done % 255 + 1;
}

/*
 * APPEND (catalog.h): seq, term, prev index (2), prev term, entry term,
 * commit distance, entry.
 *
 * The usual Raft follower rules:
 * - An older term is a stale leader: refuse.
 * - Our log must hold the leader's entry at prev (same term), or the new
 *   entry would not fit. If it doesn't, drop the conflicting entry at prev
 *   and everything after, and tell the leader how far we do fit.
 * - Store the entry unless we already have it (same index and term).
 * - Commit up to the leader's commit index, but never past what we know
 *   matches the leader.
 */
static void on_append(const struct can_frame *f)
{
	const uint8_t *d = f->data;
	uint16_t prev = d[2] | d[3] << 8;
	uint8_t prev_term = d[4], entry_term = d[5], distance = d[6], entry = d[7];
	bool ok = false;

	k_mutex_lock(&raft_lock, K_FOREVER);

	if (d[1] >= term) {
		term = d[1];
		leader = f->id - CAN_ID_APPEND(0);
		ok = prev <= log_last_index() && log_term(prev) == prev_term;

		if (!ok && prev <= log_last_index()) {
			log_truncate(prev);
		}

		if (ok && entry != ENTRY_NONE) {
			uint16_t n = prev + 1;

			if (n > log_last_index() || log_term(n) != entry_term) {
				log_truncate(n);
				ok = log_append(entry_term, entry);	/* false: log full (rule 7) */
			}
		}
	}

	if (ok) {
		reply.match = (entry != ENTRY_NONE) ? prev + 1 : prev;

		int commit = prev + 1 - distance;

		log_set_commit(commit < reply.match ? commit : reply.match);
	} else {
		reply.match = log_last_index();
	}
	reply.ok = ok;
	reply.pending = true;

	k_mutex_unlock(&raft_lock);
	k_sem_give(&raft_sem);
}

/* CLIENT (catalog.h): seq, kind, target node, entry or leader ID, request number */
static void on_client(const struct can_frame *f)
{
	const uint8_t *d = f->data;
	uint8_t sender = f->id - CAN_ID_CLIENT(0);

	if (d[2] != NODE_ID) {
		return;		/* not for us */
	}

	switch (d[1]) {
	case CLIENT_OK:
		atomic_set(&ok_req, d[4]);
		break;
	case CLIENT_REDIRECT:
		k_mutex_lock(&raft_lock, K_FOREVER);
		leader = d[3];
		k_mutex_unlock(&raft_lock);
		atomic_set(&retry_now, 1);
		break;
	case CLIENT_REQUEST:
		k_mutex_lock(&raft_lock, K_FOREVER);
		redirect.pending = true;
		redirect.client = sender;
		redirect.req = d[4];
		k_mutex_unlock(&raft_lock);
		break;
	default:
		return;
	}

	k_sem_give(&raft_sem);
}

/* Only so RELAY can forward it: canbus receives only IDs with a handler */
static void ignore(const struct can_frame *f)
{
}

void raft_event(void)
{
	atomic_inc(&events_wanted);
	k_sem_give(&raft_sem);
}

/* ---- Raft thread ---- */

static void send_client(uint8_t kind, uint8_t target, uint8_t third, uint8_t req)
{
	uint8_t d[8] = { 0, kind, target, third, req };

	canbus_send(CAN_ID_CLIENT(NODE_ID), d);
}

/* APPEND_ACK (catalog.h): seq, term, success (0/1), match index (2) */
static void send_reply(void)
{
	k_mutex_lock(&raft_lock, K_FOREVER);
	bool pending = reply.pending;
	uint8_t d[8] = { 0, term, reply.ok, reply.match & 0xFF, reply.match >> 8 };

	reply.pending = false;
	k_mutex_unlock(&raft_lock);

	if (pending) {
		canbus_send(CAN_ID_APPEND_ACK(NODE_ID), d);
	}
}

static void send_redirect(void)
{
	k_mutex_lock(&raft_lock, K_FOREVER);
	bool pending = redirect.pending && leader != NODE_ID;
	uint8_t client = redirect.client, req = redirect.req, to = leader;

	redirect.pending = false;
	k_mutex_unlock(&raft_lock);

	if (pending) {
		send_client(CLIENT_REDIRECT, client, to, req);
	}
}

/* The oldest unOKed EVENT: send it, then again every CLIENT_RETRY_MS */
static void send_event(void)
{
	static int64_t sent_ms;
	static uint8_t sent_req;
	int done = atomic_get(&events_done);
	uint8_t req = req_number(done);

	if (done == atomic_get(&events_wanted)) {
		return;		/* nothing waiting */
	}

	if (atomic_get(&ok_req) == req) {
		atomic_inc(&events_done);
		printk("[N%d] RAFT EVENT req=%d OK\n", NODE_ID, req);
		return;		/* the next one (if any) goes out on the next pass */
	}

	int64_t now = k_uptime_get();

	if (req != sent_req || atomic_clear(&retry_now) || now - sent_ms >= CLIENT_RETRY_MS) {
		k_mutex_lock(&raft_lock, K_FOREVER);
		uint8_t to = leader;
		k_mutex_unlock(&raft_lock);

		send_client(CLIENT_REQUEST, to, ENTRY(ENTRY_EVENT, NODE_ID), req);
		sent_ms = now;
		sent_req = req;
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
	while (1) {
		/* Woken by a frame or a press; the timeout drives EVENT retries */
		k_sem_take(&raft_sem, K_MSEC(CLIENT_RETRY_MS));
		send_reply();		/* first: the leader is waiting for it */
		send_redirect();
		send_event();
		print_commits();
	}
}

K_THREAD_DEFINE(raft_thread, 2048, raft_loop, NULL, NULL, NULL, RAFT_PRIO, 0, SYS_FOREVER_MS);

int raft_init(void)
{
	raftlog_init();

	for (int n = 1; n <= RAFT_N; n++) {
		if (n != NODE_ID && (canbus_on(CAN_ID_APPEND(n), on_append) != 0 ||
				     canbus_on(CAN_ID_CLIENT(n), on_client) != 0)) {
			return -ENOMEM;
		}
	}

	/* The other STM32's ACKs reach the Pi through us when we relay */
	if (canbus_on(CAN_ID_APPEND_ACK(OTHER_STM32), ignore) != 0) {
		return -ENOMEM;
	}

	k_thread_start(raft_thread);
	return 0;
}
