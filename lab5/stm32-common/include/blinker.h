#ifndef BLINKER_H
#define BLINKER_H

/*
 * The lamps follow BLINK from the cockpit, not a timer here, so the front
 * and rear nodes stay in step. Only with no cockpit (no HB_N1 on either bus)
 * does a node flash hazards on its own timer. Each node's overlay has
 * blink_left/blink_right.
 */

/* Call before canbus_init() (registers the BLINK handler) */
int blinker_init(void);

#endif
