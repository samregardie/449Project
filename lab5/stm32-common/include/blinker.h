#ifndef BLINKER_H
#define BLINKER_H

/*
 * The lamps run off global time (globaltime.h), so the front and rear nodes
 * flash in step. BLINK from the cockpit sets only the state (left, right,
 * hazard). With no cockpit (no HB_N1 on either bus) a node flashes hazards
 * at 4 Hz on its own. Each node's overlay has blink_left/blink_right.
 */

/* Call before canbus_init() (registers the BLINK handler) */
int blinker_init(void);

#endif
