#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

enum side {
	SIDE_LEFT,
	SIDE_RIGHT,
};

/* Configure the A/B pins and their interrupts */
int encoder_init(void);

/* Raw signed tick count since init; sign follows the direction of travel */
int32_t encoder_count(enum side s);

/* Velocity is the tick change over the last ENCODER_WINDOW samples */
#define ENCODER_SAMPLE_MS 1
#define ENCODER_WINDOW 10	/* 10 * 1 ms = 10 ms; full speed is ~71 */

/* Call every ENCODER_SAMPLE_MS from the control thread only */
void encoder_sample(void);

/* Ticks per ENCODER_WINDOW * ENCODER_SAMPLE_MS, as of the last sample */
int32_t encoder_velocity(enum side s);

#endif
