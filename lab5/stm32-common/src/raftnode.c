/*
 * Raft agent, shared by every node. See raftnode.h.
 *
 * Handlers only change state and set "pending" flags; raftnode_poll() turns
 * the flags into frames. A flag holds at most one frame of each kind, so
 * the frame list can't overflow, and a newer reply simply replaces an older
 * one that hasn't gone out yet.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "raftnode.h"

#define CLIENT_RETRY_MS 100	/* resend an EVENT request until its OK */

static int me;
static uint8_t term;
static int leader;
static bool log_full;
static int64_t now;			/* from the last raftnode_poll() */
static bool alive[RAFT_N + 1];		/* from the last raftnode_poll() */

/* ---- Notes: lines to print, collected here and printed by the caller ---- */

static char notes[384];
static size_t notes_len;

static void note(const char *fmt, ...)
{
	va_list args;

	if (notes_len >= sizeof(notes) - 1) {
		return;		/* full: drop it rather than print under the lock */
	}
	va_start(args, fmt);
	int n = vsnprintf(notes + notes_len, sizeof(notes) - notes_len, fmt, args);
	va_end(args);

	if (n > 0) {
		notes_len += n;
		if (notes_len > sizeof(notes) - 1) {
			notes_len = sizeof(notes) - 1;	/* truncated */
		}
	}
}

void raftnode_notes(char *buf, size_t size)
{
	snprintf(buf, size, "%s", notes);
	notes[0] = '\0';
	notes_len = 0;
}

static bool is_leader(void)
{
	return leader == me;
}

/* A node ID we can hear from: another node of the cluster */
static bool is_peer(int node)
{
	return node >= 1 && node <= RAFT_N && node != me;
}

/* ---- Leader ---- */

static uint16_t next_index[RAFT_N + 1];	/* next entry to send each follower */
static uint16_t match_index[RAFT_N + 1];	/* last entry it confirmed */
static bool append_now;				/* send an APPEND on the next poll */

/*
 * EVENT requests (rule 5). A retry repeats the request number, so a number
 * already taken is not appended again. Indexed by node ID, ourselves too:
 * the leader's own EVENTs go through the same path.
 */
static uint8_t last_req[RAFT_N + 1];	/* last request number taken from each node */
static uint16_t event_index[RAFT_N + 1];	/* its log index until it commits, 0 = none */
static bool ok_pending[RAFT_N + 1];	/* committed (or a repeat): send CLIENT_OK */

/* Our own entry. Returns its index, 0 if the log is full */
static uint16_t leader_append(uint8_t entry)
{
	if (!log_append(term, entry)) {
		log_full = true;	/* our own entries are always valid, so it's full */
		return 0;
	}
	append_now = true;	/* one APPEND per batch: the ACKs pull the rest */
	return log_last_index();
}

/* A request to log an EVENT, from another node or from ourselves */
static void take_request(int node, uint8_t entry, uint8_t req)
{
	if (req != last_req[node]) {
		uint16_t index = leader_append(entry);

		if (index != 0) {	/* 0: log full, not taken; the client keeps retrying */
			last_req[node] = req;
			event_index[node] = index;
			if (RAFT_DEBUG) {
				note("[N%d] RAFT EVENT from N%d req=%d -> idx=%d\n", me, node, req,
				     index);
			}
		}
	} else if (event_index[node] == 0) {
		ok_pending[node] = true;	/* OKed already, so the OK was lost: resend */
	}
}

/*
 * The index the next APPEND is about. APPEND is one broadcast frame, so it
 * carries the next entry for the follower furthest behind (rule 9).
 * Followers we can't hear are skipped, or one unplugged node would hold
 * everyone back. When everyone is up to date it is past the end of the log:
 * entry 0, the keep-alive. *behind = that follower, 0 if none.
 */
static uint16_t next_to_send(int *behind)
{
	uint16_t n = log_last_index() + 1;

	*behind = 0;
	for (int node = 1; node <= RAFT_N; node++) {
		if (is_peer(node) && alive[node] && next_index[node] < n) {
			n = next_index[node];
			*behind = node;
		}
	}
	return n;
}

/* APPEND (catalog.h): seq, term, prev index (2), prev term, entry term, commit distance, entry */
static void build_append(struct raft_frame *f)
{
	int behind;
	uint16_t n = next_to_send(&behind);
	uint16_t prev = n - 1, commit = log_commit_index();
	int distance = n - commit;	/* >= 1 on a keep-alive: commit <= last < n */

	if (distance < 0) {
		distance = 0;
	}
	if (distance > 255) {
		distance = 255;	/* too low a commit index is safe: the follower catches up later */
	}

	uint8_t d[8] = { 0, term, prev & 0xFF, prev >> 8, log_term(prev), log_term(n), distance,
			 log_entry(n) };

	f->id = CAN_ID_APPEND(me);
	memcpy(f->data, d, 8);

	/* Debug: each APPEND with an entry once. A resend of the same index for the
	 * same follower (every 100 ms while it doesn't ACK) prints nothing more, so
	 * a stuck follower shows as one line, not a stream. */
	static uint16_t printed_n;
	static int printed_for;

	if (RAFT_DEBUG && d[7] != ENTRY_NONE && (n != printed_n || behind != printed_for)) {
		note("[N%d] RAFT APPEND idx=%d term=%d %s (0x%02x) commit=%d for N%d\n", me, n,
		     log_term(n), entry_name(d[7]), d[7], commit, behind);
		printed_n = n;
		printed_for = behind;
	}
}

/* Commit the newest entry of this term that a majority has (we count
 * ourselves). Older-term entries commit along with it */
static void advance_commit(void)
{
	for (uint16_t i = log_last_index(); i > log_commit_index(); i--) {
		int count = 1;

		for (int node = 1; node <= RAFT_N; node++) {
			count += is_peer(node) && match_index[node] >= i;
		}
		if (count >= RAFT_MAJORITY && log_term(i) == term) {
			log_set_commit(i);
			return;
		}
	}
}

/*
 * APPEND_ACK (catalog.h): seq, term, success (0/1), match index (2).
 *
 * Success: the follower has everything up to `match`. Only ever raise
 * match_index on success: every follower ACKs every APPEND, so an up-to-date
 * follower ACKs the old entries we resend for a lagging one, with a low
 * `match`. Taking that at face value made it look behind too, and each ACK
 * triggered another APPEND (a flood).
 * Failure: its log didn't fit at prev, and `match` is its last index after
 * dropping what conflicted, so we resend from there. A follower that rebooted
 * (RAM log) reports 0 and gets the whole log again.
 *
 * Catch-up: send the next APPEND right away, not on the 100 ms keep-alive,
 * when this ACK moved the follower and the next APPEND is for it. Not on
 * every ACK: all followers ACK every APPEND, so with two behind each APPEND
 * would trigger two more. This way it is one APPEND per entry.
 */
void raftnode_on_ack(int from, const uint8_t d[8])
{
	uint16_t match = d[3] | d[4] << 8;

	if (!is_leader() || !is_peer(from)) {
		return;
	}

	uint16_t before = next_index[from];

	if (match > log_last_index()) {
		match = log_last_index();
	}
	if (d[2]) {
		if (match > match_index[from]) {
			match_index[from] = match;
		}
		next_index[from] = match_index[from] + 1;
	} else {
		if (match < match_index[from]) {
			match_index[from] = match;	/* it lost entries: stop counting them */
		}
		if (match == 0) {	/* empty log: it rebooted, and its request */
			last_req[from] = 0;	/* numbers start again at 1 */
			event_index[from] = 0;
		}
		next_index[from] = match + 1;
	}

	advance_commit();

	int behind;

	if (next_index[from] != before && next_index[from] <= log_last_index() &&
	    next_to_send(&behind) == next_index[from]) {
		append_now = true;
	}
	if (RAFT_DEBUG && !d[2] && next_index[from] != before) {
		note("[N%d] RAFT ACK N%d failed, resend from %d\n", me, from, next_index[from]);
	}
}

void raftnode_keepalive(void)
{
	if (is_leader()) {
		append_now = true;
	}
}

void raftnode_log_network(const struct network *want)
{
	if (!is_leader()) {
		return;
	}

	struct network have = log_latest_network();
	uint8_t entries[NETWORK_DIFF_MAX];
	int count = network_diff(&have, want, entries);

	for (int i = 0; i < count; i++) {
		leader_append(entries[i]);
	}
}

/* ---- Follower ---- */

/* The reply to the latest APPEND. If two APPENDs arrive before a poll, only
 * the newer reply goes out: it describes our log as it is now */
static struct {
	bool pending;
	bool ok;
	uint16_t match;
} reply;

/*
 * APPEND (catalog.h). The usual Raft follower rules:
 * - An older term is a stale leader: refuse.
 * - Our log must hold the leader's entry at prev (same term), or the new
 *   entry would not fit. If it doesn't, drop the conflicting entry at prev
 *   and everything after, and tell the leader how far we do fit.
 * - Store the entry unless we already have it (same index and term).
 * - Commit up to the leader's commit index, but never past what we know
 *   matches the leader.
 */
void raftnode_on_append(int from, const uint8_t d[8])
{
	uint16_t prev = d[2] | d[3] << 8;
	uint8_t prev_term = d[4], entry_term = d[5], distance = d[6], entry = d[7];
	bool ok = false;

	if (!is_peer(from)) {
		return;
	}

	if (d[1] >= term) {
		term = d[1];
		leader = from;
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
}

/* ---- Client ---- */

/* A REQUEST sent to us although we don't lead: tell its client who does */
static struct {
	bool pending;
	uint8_t client;
	uint8_t req;
} redirect;

/* Our own EVENTs. Request numbers run 1..255, then wrap */
static int events_wanted;	/* raftnode_event() calls */
static int events_done;		/* OKed by the leader */
static uint8_t ok_req;		/* request number of the last OK for us */
static bool retry_now;		/* a REDIRECT told us the leader: resend now */
static int64_t sent_ms;
static uint8_t sent_req;

static uint8_t req_number(int done)
{
	return done % 255 + 1;
}

void raftnode_event(void)
{
	events_wanted++;
}

/* CLIENT (catalog.h): seq, kind, target node, entry or leader ID, request number */
void raftnode_on_client(int from, const uint8_t d[8])
{
	if (!is_peer(from) || d[2] != me) {
		return;		/* not for us */
	}

	switch (d[1]) {
	case CLIENT_REQUEST:
		/* Rule 6: only an EVENT, and only for its own node */
		if (!entry_valid(d[3]) || ENTRY_TYPE(d[3]) != ENTRY_EVENT || ENTRY_NODE(d[3]) != from) {
			return;
		}
		if (is_leader()) {
			take_request(from, d[3], d[4]);
		} else {
			redirect.pending = true;
			redirect.client = from;
			redirect.req = d[4];
		}
		break;
	case CLIENT_OK:
		ok_req = d[4];
		break;
	case CLIENT_REDIRECT:
		if (d[3] >= 1 && d[3] <= RAFT_N) {
			leader = d[3];
			retry_now = true;
		}
		break;
	default:
		break;
	}
}

static void client_frame(struct raft_frame *f, uint8_t kind, uint8_t target, uint8_t third,
			 uint8_t req)
{
	uint8_t d[8] = { 0, kind, target, third, req };

	f->id = CAN_ID_CLIENT(me);
	memcpy(f->data, d, 8);
}

/* The oldest un-OKed EVENT: request it, then again every CLIENT_RETRY_MS.
 * When we lead, the request goes straight to take_request(), no frame */
static int poll_event(struct raft_frame *f)
{
	uint8_t req = req_number(events_done);

	if (events_done == events_wanted) {
		return 0;	/* nothing waiting */
	}

	if (ok_req == req) {
		events_done++;
		note("[N%d] RAFT EVENT req=%d OK\n", me, req);
		return 0;	/* the next one (if any) goes out on the next poll */
	}

	if (req == sent_req && !retry_now && now - sent_ms < CLIENT_RETRY_MS) {
		return 0;
	}
	sent_ms = now;
	sent_req = req;
	retry_now = false;

	uint8_t entry = ENTRY(ENTRY_EVENT, me);

	if (is_leader()) {
		take_request(me, entry, req);
		return 0;
	}
	if (leader == RAFT_NO_LEADER) {
		return 0;	/* nobody to ask yet: retried later */
	}
	client_frame(f, CLIENT_REQUEST, leader, entry, req);
	return 1;
}

/* ---- Poll ---- */

void raftnode_init(int node)
{
	raftlog_init();
	me = node;
	/* With a default leader every node boots in term 1 with it (Part 2) */
	term = RAFT_DEFAULT_LEADER != RAFT_NO_LEADER ? 1 : 0;
	leader = RAFT_DEFAULT_LEADER;
	for (int n = 0; n <= RAFT_N; n++) {
		next_index[n] = 1;
	}
}

int raftnode_poll(int64_t now_ms, const bool heard[RAFT_N + 1],
		  struct raft_frame out[RAFT_FRAMES_MAX])
{
	int count = 0;

	now = now_ms;
	memcpy(alive, heard, sizeof(alive));

	/* APPEND_ACK (catalog.h): seq, term, success (0/1), match index (2).
	 * First: the leader is waiting for it */
	if (reply.pending) {
		uint8_t ack[8] = { 0, term, reply.ok, reply.match & 0xFF, reply.match >> 8 };

		reply.pending = false;
		out[count].id = CAN_ID_APPEND_ACK(me);
		memcpy(out[count++].data, ack, 8);
	}

	if (redirect.pending) {
		redirect.pending = false;
		if (!is_leader() && leader != RAFT_NO_LEADER) {
			client_frame(&out[count++], CLIENT_REDIRECT, redirect.client, leader,
				     redirect.req);
		}
	}

	/* Before the APPEND: our own EVENT, when we lead, appends here */
	struct raft_frame request;
	int requested = poll_event(&request);

	if (is_leader()) {
		if (append_now) {
			append_now = false;
			build_append(&out[count++]);
		}

		/* CLIENT_OK once an EVENT is committed (not before: an OK means it
		 * can't be lost) */
		for (int node = 1; node <= RAFT_N; node++) {
			if (event_index[node] != 0 && log_commit_index() >= event_index[node]) {
				event_index[node] = 0;
				ok_pending[node] = true;
			}
			if (ok_pending[node]) {
				ok_pending[node] = false;
				if (node == me) {
					ok_req = last_req[me];	/* our own: no frame */
				} else {
					client_frame(&out[count++], CLIENT_OK, node, 0, last_req[node]);
				}
			}
		}
	}

	if (requested) {
		out[count++] = request;
	}

	return count;
}

uint8_t raftnode_term(void)
{
	return term;
}

int raftnode_leader(void)
{
	return leader;
}

bool raftnode_log_full(void)
{
	return log_full;
}
