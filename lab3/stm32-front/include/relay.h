#ifndef RELAY_H
#define RELAY_H

/*
 * RELAY (N2 only): when N1 and N3 have no bus in common, forward their
 * frames to the other bus unchanged (same ID and seq, so receivers dedup).
 */

/* Call after errorstate_init() and before canbus_init() */
int relay_init(void);

#endif
