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

/* Wheel velocity; the team picks and documents the units */
int32_t encoder_velocity(enum side s);

#endif
