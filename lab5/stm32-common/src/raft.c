/*
 * Raft agent on the STM32s: the follower side of log replication.
 * The log and state machine are raftlog.c, shared with the Pi.
 *
 * Two parts:
 * - on_append() runs in the CAN dispatch thread. It only updates the log and
 *   notes the reply. Sending is left to the raft thread: a send can block for
 *   a few ms when a bus is down, and the dispatch thread also timestamps REF.
 * - The raft thread sends that reply (APPEND_ACK), then applies newly
 *   committed entries and prints them.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "canbus.h"
#include "raft.h"
#include "raftlog.h"

/* Below the CAN, control and HB threads (1-3) */
#define RAFT_PRIO K_PRIO_PREEMPT(4)

/* The log (raftlog.c is not thread-safe), the term and the pending reply */
K_MUTEX_DEFINE(raft_lock);

/* Given on every APPEND, so the raft thread wakes */
K_SEM_DEFINE(raft_sem, 0, 1);

/* With a default leader every node boots in term 1 (Part 2) */
static uint8_t term = RAFT_DEFAULT_LEADER != RAFT_NO_LEADER ? 1 : 0;

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
	}
}

static void raft_loop(void *p1, void *p2, void *p3)
{
	while (1) {
		k_sem_take(&raft_sem, K_FOREVER);
		send_reply();		/* first: the leader is waiting for it */
		print_commits();
	}
}

K_THREAD_DEFINE(raft_thread, 2048, raft_loop, NULL, NULL, NULL, RAFT_PRIO, 0, SYS_FOREVER_MS);

int raft_init(void)
{
	raftlog_init();

	/* Follow whichever node leads */
	for (int n = 1; n <= RAFT_N; n++) {
		if (canbus_on(CAN_ID_APPEND(n), on_append) != 0) {
			return -ENOMEM;
		}
	}

	k_thread_start(raft_thread);
	return 0;
}
