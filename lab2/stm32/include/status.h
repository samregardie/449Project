#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>

/* Currents: +/-CURRENT_FULL_SCALE_MA maps to +/-100 in the frame */
#define CURRENT_FULL_SCALE_MA 1000

#define STATUS_PERIOD_MS 20

/* mA -> the -100..100 value sent in the frame (clamped) */
int8_t status_scale_current(int ma);

/* Start sending status frames; call after current_sense_init() and picom_init() */
int status_init(void);

#endif
