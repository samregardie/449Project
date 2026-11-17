/*
 * Raft agent on the STM32s: the follower side of log replication.
 * The log and state machine are raftlog.c, shared with the Pi.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "canbus.h"
#include "raft.h"
#include "raftlog.h"

/* Below the CAN and control threads (1-3): it only prints */
#define PRINT_PRIO K_PRIO_PREEMPT(5)

/* The log (raftlog.c is not thread-safe) and the term */
K_MUTEX_DEFINE(raft_lock);

/* Given when the commit index moves, so the print thread wakes */
K_SEM_DEFINE(commit_sem, 0, 1);

/* With a default leader every node boots in term 1 (Part 2) */
static uint8_t term = RAFT_DEFAULT_LEADER != RAFT_NO_LEADER ? 1 : 0;

/* APPEND_ACK (catalog.h): seq, term, success (0/1), match index (2) */
static void send_ack(uint8_t ack_term, bool ok, uint16_t match)
{
	uint8_t d[8] = { 0, ack_term, ok, match & 0xFF, match >> 8 };

	canbus_send(CAN_ID_APPEND_ACK(NODE_ID), d);
}

/*
 * APPEND (catalog.h): seq, term, prev index (2), prev term, entry term,
 * commit distance, entry. Runs in the CAN dispatch thread: no printing here.
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
	uint16_t match;

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
		match = (entry != ENTRY_NONE) ? prev + 1 : prev;

		int commit = prev + 1 - distance;
		uint16_t before = log_commit_index();

		log_set_commit(commit < match ? commit : match);
		if (log_commit_index() != before) {
			k_sem_give(&commit_sem);
		}
	} else {
		match = log_last_index();
	}

	uint8_t ack_term = term;

	k_mutex_unlock(&raft_lock);

	send_ack(ack_term, ok, match);
}

/* Apply newly committed entries and print them, the same lines on every node */
static void print_loop(void *p1, void *p2, void *p3)
{
	while (1) {
		k_sem_take(&commit_sem, K_FOREVER);

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
		}

		char buf[96];

		k_mutex_lock(&raft_lock, K_FOREVER);
		struct network net = get_network();
		k_mutex_unlock(&raft_lock);

		network_format(&net, buf, sizeof(buf));
		printk("[N%d] NET %s\n", NODE_ID, buf);
	}
}

K_THREAD_DEFINE(raft_print, 1024, print_loop, NULL, NULL, NULL, PRINT_PRIO, 0, SYS_FOREVER_MS);

int raft_init(void)
{
	raftlog_init();

	/* Follow whichever node leads */
	for (int n = 1; n <= RAFT_N; n++) {
		if (canbus_on(CAN_ID_APPEND(n), on_append) != 0) {
			return -ENOMEM;
		}
	}

	k_thread_start(raft_print);
	return 0;
}
