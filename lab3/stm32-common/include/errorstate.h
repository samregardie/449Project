#ifndef ERRORSTATE_H
#define ERRORSTATE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Error state, HB_TX, HB_MON and the test button (B1). In error while any
 * of: an own fault (test button, bad CTRL), another zone's HB reports a
 * fault, a zone is silent on both buses, or no valid CTRL yet. Our HB
 * carries only own faults, so zones don't hold each other in error. Losing
 * the wheel link is the Pi's own fault, so it arrives in HB_N1.
 */

/* 3 missed heartbeats at 20 ms */
#define ZONE_TIMEOUT_MS 60

/* A bad CTRL keeps us in error at least this long, so our HBs report it */
#define BAD_CTRL_HOLD_MS 500

/* Called when the error state flips, maybe from an ISR: must not block */
typedef void (*errorstate_cb_t)(bool error);

/* Call before canbus_init() (registers the HB handlers). Starts in error */
int errorstate_init(errorstate_cb_t on_change);

/* Valid CTRL: clears the bad-CTRL fault once BAD_CTRL_HOLD_MS has passed */
void errorstate_ctrl_ok(void);

/* Out-of-range CTRL: own fault, sends FAULT */
void errorstate_bad_ctrl(void);

#endif
