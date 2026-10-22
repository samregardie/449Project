#ifndef ERRORSTATE_H
#define ERRORSTATE_H

#include <stdbool.h>

/*
 * Error state, HB_TX and HB_MON. In error while any of: an own fault (test
 * button, bad CTRL), another zone's HB reports a fault, a zone is silent on
 * both buses, or no valid CTRL for 60 ms. Our HB carries only own faults,
 * so zones don't hold each other in error.
 */

/* 3 missed heartbeats or CTRLs at 20 ms */
#define ZONE_TIMEOUT_MS 60

/* Called when the error state flips, maybe from an ISR: must not block */
typedef void (*errorstate_cb_t)(bool error);

/* Call before canbus_init() (registers the HB handlers). Starts in error */
int errorstate_init(errorstate_cb_t on_change);

/* Valid CTRL: clears the bad-CTRL fault, restarts the CTRL timeout */
void errorstate_ctrl_ok(void);

/* Out-of-range CTRL: own fault, sends FAULT */
void errorstate_bad_ctrl(void);

/* Debounced test button press: one enters, a second within 500 ms exits */
void errorstate_test_pressed(void);

#endif
