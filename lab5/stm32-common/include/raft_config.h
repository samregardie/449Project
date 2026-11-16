/*
 * Raft config (Lab 5, Part 2). Identical on every node: the two STM32 apps
 * and the Pi all include this one file. Plain C, no Zephyr.
 *
 * To boot without a default leader (checkoff LEADER=random), override
 * RAFT_DEFAULT_LEADER at build time on all three nodes:
 *   west build-front -p -- -DRAFT_DEFAULT_LEADER=0
 *   west build-rear  -p -- -DRAFT_DEFAULT_LEADER=0
 *   (Pi) RAFT_DEFAULT_LEADER=0 sudo -E ./run.sh
 * The same works for RAFT_SEED. Everything else is fixed here.
 * CMake remembers -D values, so go back to the default with -p and no -D.
 */
#ifndef RAFT_CONFIG_H
#define RAFT_CONFIG_H

/* Node IDs (the handout's "nodes") */
#define NODE_COCKPIT	1	/* N1, Pi */
#define NODE_STEERING	2	/* N2, front STM32 */
#define NODE_DRIVE	3	/* N3, rear STM32 */

#define RAFT_N		3	/* cluster size; node IDs are 1..RAFT_N */
#define RAFT_MAJORITY	(RAFT_N / 2 + 1)

/* Pre-defined leader: every node boots in term 1 with it, no election.
 * RAFT_NO_LEADER boots with an election instead. */
#define RAFT_NO_LEADER	0
#ifndef RAFT_DEFAULT_LEADER
#define RAFT_DEFAULT_LEADER	NODE_COCKPIT
#endif

/* Seeds the election jitter. Each node uses RAFT_SEED + its node ID, so the
 * three timeouts differ but are the same on every run. Pick the seed that
 * makes a given node win by trying values. */
#ifndef RAFT_SEED
#define RAFT_SEED	1
#endif

#define RAFT_HEARTBEAT_MS		20	/* REF period, also the clock-sync period */
#define RAFT_MISSED_HEARTBEATS		3
#define RAFT_ELECTION_JITTER_MS		39	/* timeout = 3 x 20 + U(0, 39) ms */
#define RAFT_ELECTION_TIMEOUT_MIN_MS	(RAFT_MISSED_HEARTBEATS * RAFT_HEARTBEAT_MS)

#define RAFT_LOG_SIZE	30000	/* entries */

/* 1: print elections, votes and APPENDs that carry an entry (not every REF
 * or empty APPEND, which would bury everything else) */
#define RAFT_DEBUG	1

/* Catch typos at build time, on every node */
_Static_assert(RAFT_LOG_SIZE <= 65535, "log index is 2 bytes in APPEND");
_Static_assert(RAFT_N <= 15, "node ID is 4 bits in a log entry");
_Static_assert(RAFT_DEFAULT_LEADER >= 0 && RAFT_DEFAULT_LEADER <= RAFT_N,
	       "default leader must be a node ID or RAFT_NO_LEADER");
_Static_assert(RAFT_ELECTION_TIMEOUT_MIN_MS > RAFT_HEARTBEAT_MS,
	       "election timeout must be longer than the heartbeat period");

#endif
