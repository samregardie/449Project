#ifndef BLINKER_H
#define BLINKER_H

/*
 * The lamps follow BLINK from the cockpit, with no timer here, so the front
 * and rear nodes stay in step. Each node's overlay has blink_left/blink_right.
 */

/* Call before canbus_init() (registers the BLINK handler) */
int blinker_init(void);

#endif
