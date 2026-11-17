/*
 * Replicated log and state machine. See raftlog.h. Two plain arrays, so
 * each entry costs 2 bytes: index i lives in slot i - 1.
 */

#include <stdio.h>
#include <string.h>

#include "raftlog.h"

static uint8_t terms[RAFT_LOG_SIZE];
static uint8_t entries[RAFT_LOG_SIZE];
static uint16_t last;		/* index of the last entry, 0 = empty */
static uint16_t commit;		/* highest index known to be committed */
static uint16_t applied;	/* highest index applied to `net` */
static struct network net;

void raftlog_init(void)
{
	last = commit = applied = 0;
	memset(&net, 0, sizeof(net));

	for (int n = 1; n <= RAFT_N; n++) {
		net.node_up[n] = true;
		net.link_up[n][0] = true;
		net.link_up[n][1] = true;
	}
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
	return true;
}

void log_truncate(uint16_t index)
{
	if (index <= commit) {
		index = commit + 1;
	}
	if (index <= last) {
		last = index - 1;
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

/* ---- State machine ---- */

/* The four mutators of Part 4 */
static void add_link(int node, int bus)
{
	net.link_up[node][bus] = true;
}

static void drop_link(int node, int bus)
{
	net.link_up[node][bus] = false;
}

static void add_node(int node)
{
	net.node_up[node] = true;	/* links stay down until their LINK_*_UP */
}

static void drop_node(int node)
{
	net.node_up[node] = false;
	net.link_up[node][0] = false;
	net.link_up[node][1] = false;
}

static void apply(uint8_t entry)
{
	int node = ENTRY_NODE(entry);

	switch (ENTRY_TYPE(entry)) {
	case ENTRY_COCKPIT_OK:
		net.cockpit_ok = true;
		break;
	case ENTRY_COCKPIT_ERROR:
		net.cockpit_ok = false;
		break;
	case ENTRY_NODE_UP:
		add_node(node);
		break;
	case ENTRY_NODE_DOWN:
		drop_node(node);
		break;
	case ENTRY_LINK_A_UP:
		add_link(node, 0);
		break;
	case ENTRY_LINK_A_DOWN:
		drop_link(node, 0);
		break;
	case ENTRY_LINK_B_UP:
		add_link(node, 1);
		break;
	case ENTRY_LINK_B_DOWN:
		drop_link(node, 1);
		break;
	default:
		break;	/* EVENT: recorded in the log, no change to the network */
	}
}

uint16_t log_apply_next(void)
{
	if (applied == commit) {
		return 0;
	}

	applied++;
	apply(entries[applied - 1]);
	return applied;
}

struct network get_network(void)
{
	return net;
}

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
	};
	uint8_t type = ENTRY_TYPE(entry);

	return type <= ENTRY_EVENT ? names[type] : "INVALID";
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
