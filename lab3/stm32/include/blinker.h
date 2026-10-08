#ifndef BLINKER_H
#define BLINKER_H

#include <stdbool.h>
#include <stdint.h>

/* Steering is -100..100 (see servo.h); 30 = 30% of full lock */
#define BLINKER_TURN_THRESHOLD 30

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
