/*
 * Cloud mirror (Lab 5 extra credit): publishes committed log entries and a
 * 1 s heartbeat to Redis on the cloud VM, best effort.
 *
 * The real-time threads only copy into memory here (publisher_commit,
 * publisher_status). One normal-priority thread does all the network I/O,
 * so a slow or dead uplink never delays REF, APPEND or CTRL.
 */
#ifndef PUBLISHER_H
#define PUBLISHER_H

#include <stdbool.h>
#include <stdint.h>

// Start the publisher thread. Call before main() switches to SCHED_FIFO so
// this thread keeps normal priority. Does nothing if REDIS_PASSWORD is unset.
void publisher_start(void);

// One committed entry, from print_commits()
void publisher_commit(uint16_t index, uint8_t term, uint8_t entry, bool replayed,
                      int64_t global_us);

// What the next heartbeat reports, from tick() once a second
void publisher_status(int64_t global_us, int leader, const char *network);

#endif
