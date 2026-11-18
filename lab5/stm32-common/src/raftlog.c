/*
 * Replicated log and state machine. See raftlog.h. Two plain arrays, so
 * each entry costs 2 bytes: index i lives in slot i - 1.
 *
 * Two copies of the network:
 * - committed: committed entries, applied in order (getNetwork(), Part 4)
 * - latest:    every entry in the log, committed or not. Only the leader
 *              uses it, to tell whether a new entry would change anything
 *              (rule 1) while earlier ones are still waiting to commit.
 */

#include <stdio.h>
#include <string.h>

#include "raftlog.h"

static uint8_t terms[RAFT_LOG_SIZE];
static uint8_t entries[RAFT_LOG_SIZE];
static uint16_t last;		/* index of the last entry, 0 = empty */
static uint16_t commit;		/* highest index known to be committed */
static uint16_t applied;	/* highest index applied to `committed` */
static struct network committed;
static struct network latest;

/* ---- State machine ---- */

/* Before any entry: every node and link up, cockpit in error (all boot in error) */
static struct network initial_network(void)
{
	struct network n;

	memset(&n, 0, sizeof(n));
	for (int node = 1; node <= RAFT_N; node++) {
		n.node_up[node] = true;
		n.link_up[node][0] = true;
		n.link_up[node][1] = true;
	}
	return n;
}

/* The four mutators of Part 4 */
static void add_link(struct network *n, int node, int bus)
{
	n->link_up[node][bus] = true;
}

static void drop_link(struct network *n, int node, int bus)
{
	n->link_up[node][bus] = false;
}

static void add_node(struct network *n, int node)
{
	n->node_up[node] = true;	/* links stay down until their LINK_*_UP */
}

static void drop_node(struct network *n, int node)
{
	n->node_up[node] = false;
	n->link_up[node][0] = false;
	n->link_up[node][1] = false;
}

static void apply(struct network *n, uint8_t entry)
{
	int node = ENTRY_NODE(entry);

	switch (ENTRY_TYPE(entry)) {
	case ENTRY_COCKPIT_OK:
		n->cockpit_ok = true;
		break;
	case ENTRY_COCKPIT_ERROR:
		n->cockpit_ok = false;
		break;
	case ENTRY_NODE_UP:
		add_node(n, node);
		break;
	case ENTRY_NODE_DOWN:
		drop_node(n, node);
		break;
	case ENTRY_LINK_A_UP:
		add_link(n, node, 0);
		break;
	case ENTRY_LINK_A_DOWN:
		drop_link(n, node, 0);
		break;
	case ENTRY_LINK_B_UP:
		add_link(n, node, 1);
		break;
	case ENTRY_LINK_B_DOWN:
		drop_link(n, node, 1);
		break;
	default:
		break;	/* EVENT, LEADER: recorded in the log, no change to the network */
	}
}

void raftlog_init(void)
{
	last = commit = applied = 0;
	committed = initial_network();
	latest = initial_network();
}

/* ---- Log ---- */

uint16_t log_last_index(void)
{
	return last;
}

uint8_t log_term(uint16_t index)
{
	return (index >= 1 && index <= last) ? terms[index - 1] : 0;
}

uint8_t log_entry(uint16_t index)
{
	return (index >= 1 && index <= last) ? entries[index - 1] : 0;
}

bool log_append(uint8_t term, uint8_t entry)
{
	if (last == RAFT_LOG_SIZE || !entry_valid(entry)) {
		return false;
	}

	terms[last] = term;
	entries[last] = entry;
	last++;
	apply(&latest, entry);
	return true;
}

void log_truncate(uint16_t index)
{
	if (index <= commit) {
		index = commit + 1;
	}
	if (index > last) {
		return;
	}

	last = index - 1;

	/* Rebuild `latest` from the entries that remain (rare: only on a conflict) */
	latest = initial_network();
	for (uint16_t i = 0; i < last; i++) {
		apply(&latest, entries[i]);
	}
}

uint16_t log_commit_index(void)
{
	return commit;
}

void log_set_commit(uint16_t index)
{
	if (index > last) {
		index = last;
	}
	if (index > commit) {
		commit = index;
	}
}

uint16_t log_apply_next(void)
{
	if (applied == commit) {
		return 0;
	}

	applied++;
	apply(&committed, entries[applied - 1]);
	return applied;
}

struct network get_network(void)
{
	return committed;
}

struct network log_latest_network(void)
{
	return latest;
}

/* network_diff() helper: record one entry and apply it to the working copy */
static void add_entry(struct network *n, uint8_t out[], int *count, uint8_t entry)
{
	out[(*count)++] = entry;
	apply(n, entry);
}

/*
 * Walk from `have` to `want` one entry at a time, applying each to a copy so
 * the next comparison sees its effect. That gives the rule-4 order for free:
 * NODE_UP leaves the node with no links, so its LINK_*_UP entries follow.
 * At most 1 + 3 per node entries, so NETWORK_DIFF_MAX always fits.
 */
int network_diff(const struct network *have, const struct network *want,
		 uint8_t out[NETWORK_DIFF_MAX])
{
	struct network n = *have;
	int count = 0;

	if (want->cockpit_ok != n.cockpit_ok) {
		add_entry(&n, out, &count,
			  ENTRY(want->cockpit_ok ? ENTRY_COCKPIT_OK : ENTRY_COCKPIT_ERROR, 0));
	}

	for (int node = 1; node <= RAFT_N; node++) {
		if (!want->node_up[node]) {
			if (n.node_up[node]) {
				add_entry(&n, out, &count, ENTRY(ENTRY_NODE_DOWN, node));
			}
			continue;	/* NODE_DOWN dropped its links too */
		}
		if (!n.node_up[node]) {
			add_entry(&n, out, &count, ENTRY(ENTRY_NODE_UP, node));
		}
		if (want->link_up[node][0] != n.link_up[node][0]) {
			add_entry(&n, out, &count,
				  ENTRY(want->link_up[node][0] ? ENTRY_LINK_A_UP : ENTRY_LINK_A_DOWN, node));
		}
		if (want->link_up[node][1] != n.link_up[node][1]) {
			add_entry(&n, out, &count,
				  ENTRY(want->link_up[node][1] ? ENTRY_LINK_B_UP : ENTRY_LINK_B_DOWN, node));
		}
	}

	return count;
}

static bool share_a_bus(const struct network *n, int x, int y)
{
	return (n->link_up[x][0] && n->link_up[y][0]) || (n->link_up[x][1] && n->link_up[y][1]);
}

bool network_relay(const struct network *n, int me)
{
	if (!n->node_up[me] || !n->link_up[me][0] || !n->link_up[me][1]) {
		return false;	/* we can only carry frames across if we are on both buses */
	}

	for (int x = 1; x <= RAFT_N; x++) {
		for (int y = x + 1; y <= RAFT_N; y++) {
			if (x != me && y != me && n->node_up[x] && n->node_up[y] &&
			    !share_a_bus(n, x, y)) {
				return true;
			}
		}
	}
	return false;
}

/* ---- Printing ---- */

const char *entry_name(uint8_t entry)
{
	static const char *const names[] = {
		[ENTRY_NONE] = "NONE",
		[ENTRY_COCKPIT_OK] = "COCKPIT_OK",
		[ENTRY_COCKPIT_ERROR] = "COCKPIT_ERROR",
		[ENTRY_NODE_UP] = "NODE_UP",
		[ENTRY_NODE_DOWN] = "NODE_DOWN",
		[ENTRY_LINK_A_UP] = "LINK_A_UP",
		[ENTRY_LINK_A_DOWN] = "LINK_A_DOWN",
		[ENTRY_LINK_B_UP] = "LINK_B_UP",
		[ENTRY_LINK_B_DOWN] = "LINK_B_DOWN",
		[ENTRY_EVENT] = "EVENT",
		[ENTRY_LEADER] = "LEADER",
	};
	uint8_t type = ENTRY_TYPE(entry);

	return type <= ENTRY_LEADER ? names[type] : "INVALID";
}

void network_format(const struct network *n, char *buf, size_t size)
{
	size_t len = 0;

	for (int node = 1; node <= RAFT_N && len < size; node++) {
		if (n->node_up[node]) {
			len += snprintf(buf + len, size - len, "N%d=up(%s,%s) ", node,
					n->link_up[node][0] ? "A" : "-", n->link_up[node][1] ? "B" : "-");
		} else {
			len += snprintf(buf + len, size - len, "N%d=down ", node);
		}
	}

	if (len < size) {
		snprintf(buf + len, size - len, "cockpit=%s", n->cockpit_ok ? "ok" : "error");
	}
}
