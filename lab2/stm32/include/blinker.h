#ifndef BLINKER_H
#define BLINKER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Steering is signed: 0 = centre, negative = left.
 * Placeholder until the team picks a threshold in wheel units.
 */
#define BLINKER_TURN_THRESHOLD 1000

/* Configure the LED pins and start in the error state (hazards) */
int blinker_init(void);

/* Call once per button press, not while the button is held */
void blinker_left_pressed(void);
void blinker_right_pressed(void);

/* Call with every new steering value; handles self-cancel */
void blinker_steering(int32_t steering);

/* true = hazards, false = leave the error state (blinkers off) */
void blinker_set_error(bool error);

#endif
