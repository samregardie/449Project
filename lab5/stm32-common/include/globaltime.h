#ifndef GLOBALTIME_H
#define GLOBALTIME_H

#include <stdint.h>

/*
 * Global time (Lab 5, Part 6): this node's clock follows the leader's REF.
 * Drives SYNC, REF_RX and PPS; each node's overlay has sync/ref-rx/pps-gpios.
 */

/* Call before canbus_init() (registers the REF handlers) */
int globaltime_init(void);

/* Global time in us. Safe from any thread or ISR */
int64_t global_gettime(void);

#endif
