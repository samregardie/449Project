#ifndef CANBUS_H
#define CANBUS_H

#include <stdint.h>

#include <zephyr/drivers/can.h>

/* Message catalog: standard IDs, DLC 8, every frame sent on both buses */
#define CAN_ID_CTRL	0x010
#define CAN_ID_FAULT	0x020
#define CAN_ID_HB_N1	0x100
#define CAN_ID_HB_N2	0x101
#define CAN_ID_HB_N3	0x102
#define CAN_ID_BLINK	0x200
#define CAN_ID_FORCE	0x300

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
