#ifndef ERRORSTATE_H
#define ERRORSTATE_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>	/* BIT() */

/*
 * Sole owner of the error state; callers report events. ISR-safe.
 * Parser: call errorstate_frame_ok() before drive_command().
 */

/* In the error state while any bit is set */
#define ERR_POWERUP	    BIT(0)
#define ERR_LINK_LOST	BIT(1)
#define ERR_BAD_CMD	    BIT(2)
#define ERR_SELFTEST	BIT(3)	/* cleared only by errorstate_selftest_exit() */

/* 3 missed commands at a 20 ms period */
#define LINK_TIMEOUT_MS (3 * 20)

/* Valid frame: clears all but ERR_SELFTEST, restarts the link timer */
void errorstate_frame_ok(void);

/* Well-formed but out of range; malformed frames are just dropped */
void errorstate_out_of_range(void);

/* Enter on every press edge; exit on the second press of a double */
void errorstate_selftest_enter(void);
void errorstate_selftest_exit(void);

bool errorstate_active(void);
uint32_t errorstate_causes(void);	/* ERR_* mask */

#endif
