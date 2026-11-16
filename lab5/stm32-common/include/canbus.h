#ifndef CANBUS_H
#define CANBUS_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/can.h>

/*
 * Message catalog (Lab 5). This is the one copy: the Pi's receiver.c uses the
 * same IDs and layouts.
 *
 * - Standard 11-bit IDs, DLC 8, every frame sent on both buses.
 * - Lower ID wins arbitration, so the order below is the priority order.
 * - n is the node ID: 1 = N1 cockpit (Pi), 2 = N2 front, 3 = N3 rear.
 *   Anything more than one node can send has one ID per node (REF and APPEND
 *   too: two nodes can briefly both think they lead, and two senders on one
 *   ID would collide on the bus and confuse the dedup).
 * - Byte 0 of every frame: seq in the low nibble (written by canbus_send),
 *   flags in the high nibble. Multi-byte fields are little-endian.
 *
 * ID         Name           From       When                       Bytes 1..7
 * 0x010      CTRL           N1         every UDP update (~20 ms)  steer int16, throttle, brake,
 *                                                                 buttons, CRC-8 of bytes 0-5
 * 0x011+n    REF_Nn         leader     20 ms                      term, cycle counter,
 *                                                                 leader time in us (5 bytes)
 * 0x018+n    REQ_VOTE_Nn    candidate  election only              term, last log index (2),
 *                                                                 last log term
 * 0x01C+n    VOTE_Nn        voter      reply to REQ_VOTE          term, candidate ID, granted (0/1)
 * 0x020      FAULT          any        on a fault                 zone ID, reason
 * 0x040+n    APPEND_Nn      leader     100 ms, or a new entry     term, prev index (2), prev term,
 *                                                                 commit index (2), entry
 * 0x044+n    APPEND_ACK_Nn  follower   reply to APPEND            term, success (0/1),
 *                                                                 match index (2)
 * 0x048+n    CLIENT_Nn      any node   topology change or event   kind (CLIENT_*), target node ID,
 *                                                                 entry (REQUEST) or leader ID (REDIRECT),
 *                                                                 request number
 * 0x100+n-1  HB_Nn          each node  20 ms                      own fault (0/1),
 *                                                                 bus health (bit0 = A ok, bit1 = B ok)
 * 0x200      BLINK          N1         on change + every 100 ms   state (off/left/right/hazard)
 * 0x300      FORCE          N2         50 ms                      int8
 *
 * An APPEND with entry 0 (ENTRY_NONE) carries no entry: the 100 ms keep-alive.
 * CLIENT request number: the sender counts up once per new request and
 * repeats the number on a retry, so the leader can drop the repeat.
 * Bus A is the handout's CAN 1, bus B is CAN 2.
 * BLINK byte 2 (phase) is the Lab 3 timing; it goes away once the blinkers
 * run on global time.
 */
#define CAN_ID_CTRL		0x010
#define CAN_ID_REF(n)		(0x011 + (n))
#define CAN_ID_REQ_VOTE(n)	(0x018 + (n))
#define CAN_ID_VOTE(n)		(0x01C + (n))
#define CAN_ID_FAULT		0x020
#define CAN_ID_APPEND(n)	(0x040 + (n))
#define CAN_ID_APPEND_ACK(n)	(0x044 + (n))
#define CAN_ID_CLIENT(n)	(0x048 + (n))
#define CAN_ID_HB_N1		0x100
#define CAN_ID_HB_N2		0x101
#define CAN_ID_HB_N3		0x102
#define CAN_ID_BLINK		0x200
#define CAN_ID_FORCE		0x300

/* CLIENT byte 1 */
enum client_kind {
	CLIENT_REQUEST,		/* client -> leader: please append this entry */
	CLIENT_REDIRECT,	/* follower -> client: send it to the leader instead */
	CLIENT_OK,		/* leader -> client: the entry is committed */
};

/*
 * Log entry: one byte, type in the high nibble, node ID in the low nibble,
 * so it reads straight off candump (0x63 = LINK_A_DOWN for N3).
 * Never 0, because every type is 1 or more. The Pi's receiver.c has the same values.
 *
 *   0x10 COCKPIT_OK        vehicle leaves failure mode
 *   0x20 COCKPIT_ERROR     vehicle enters failure mode (Pi down or wheel link lost)
 *   0x3n NODE_UP n         addNode(n); its LINK_*_UP entries follow separately
 *   0x4n NODE_DOWN n       dropNode(n), which also drops its links
 *   0x5n LINK_A_UP n       addLink(n, A)
 *   0x6n LINK_A_DOWN n     dropLink(n, A)
 *   0x7n LINK_B_UP n       addLink(n, B)
 *   0x8n LINK_B_DOWN n     dropLink(n, B)
 *   0x9n EVENT from n
 */
enum entry_type {
	ENTRY_NONE,		/* no entry (empty APPEND) */
	ENTRY_COCKPIT_OK,
	ENTRY_COCKPIT_ERROR,
	ENTRY_NODE_UP,
	ENTRY_NODE_DOWN,
	ENTRY_LINK_A_UP,
	ENTRY_LINK_A_DOWN,
	ENTRY_LINK_B_UP,
	ENTRY_LINK_B_DOWN,
	ENTRY_EVENT,
};

#define NODE_COUNT 3	/* node IDs are 1..NODE_COUNT */

#define ENTRY(type, node)	(((type) << 4) | (node))
#define ENTRY_TYPE(e)		((e) >> 4)
#define ENTRY_NODE(e)		((e) & 0x0F)

/* Reject anything else as malformed: unknown type, or a bad node ID */
static inline bool entry_valid(uint8_t e)
{
	uint8_t type = ENTRY_TYPE(e);
	uint8_t node = ENTRY_NODE(e);

	if (type == ENTRY_COCKPIT_OK || type == ENTRY_COCKPIT_ERROR) {
		return node == 0;
	}

	return type >= ENTRY_NODE_UP && type <= ENTRY_EVENT && node >= 1 && node <= NODE_COUNT;
}

/* Byte 0: seq in the low nibble (written by canbus_send), flags in the high */
#define CAN_SEQ_MASK 0x0F

enum canbus_bus {
	BUS_A,
	BUS_B,
	BUS_COUNT,
};

/* Runs in the dispatch thread, once per frame (the second copy is dropped) */
typedef void (*canbus_handler_t)(const struct can_frame *frame);

/* Also runs in the dispatch thread, for every new frame, with the bus it came on */
typedef void (*canbus_tap_t)(const struct can_frame *frame, enum canbus_bus bus);

/* One handler per received ID. Call before canbus_init() */
int canbus_on(uint16_t id, canbus_handler_t handler);

/* At most one tap (the relay on N2). Call before canbus_init() */
void canbus_tap(canbus_tap_t tap);

/* Filter the registered IDs on both buses and start both controllers */
int canbus_init(void);

/* Bump that ID's seq once and queue on A, then B. Threads only */
int canbus_send(uint16_t id, const uint8_t data[8]);

/* Queue a received frame unchanged (same ID and seq) on one bus. Threads only */
int canbus_forward(enum canbus_bus bus, const struct can_frame *frame);

/* ms since `id` last arrived on `bus`, either copy. UINT32_MAX if never */
uint32_t canbus_ms_since(uint16_t id, enum canbus_bus bus);

#endif
