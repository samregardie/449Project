/*
 * Encoder resolution test: runs both motors at a few duties and prints the
 * ticks per WINDOW_MS window (mean, min, max). Wheels off the ground.
 * Watch the console (115200 baud on the ST-Link USB port).
 */

#include <zephyr/kernel.h>

#include "blinker.h"
#include "drive.h"

// TESTING ONLY - REMOVE
#include "encoder.h"
#include "motor.h"

#define PAST_LEFT  (-2 * BLINKER_TURN_THRESHOLD)
#define PAST_RIGHT (2 * BLINKER_TURN_THRESHOLD)

// // TESTING ONLY - REMOVE
// #define WINDOW_MS 10
// #define N_WINDOWS 100

// static int16_t d_left[N_WINDOWS], d_right[N_WINDOWS];
// static const int32_t duties[] = { 100, 250, 500, 750, 1000 };

// K_TIMER_DEFINE(window_timer, NULL, NULL);

// static void stats(const char *name, const int16_t *d)
// {
// 	int32_t sum = 0, min = INT16_MAX, max = INT16_MIN;

// 	for (int i = 0; i < N_WINDOWS; i++) {
// 		sum += d[i];
// 		min = MIN(min, d[i]);
// 		max = MAX(max, d[i]);
// 	}

// 	printk("  %-5s mean %4d  min %4d  max %4d  (%d ticks/s)\n", name,
// 	       sum / N_WINDOWS, min, max, sum * (1000 / WINDOW_MS) / N_WINDOWS);
// }

// static void resolution_test(void)
// {
// 	for (int k = 0; k < ARRAY_SIZE(duties); k++) {
// 		motor_set(SIDE_LEFT, duties[k]);
// 		motor_set(SIDE_RIGHT, duties[k]);
// 		k_msleep(1000);	/* let the speed settle */

// 		int32_t last_l = encoder_count(SIDE_LEFT);
// 		int32_t last_r = encoder_count(SIDE_RIGHT);

// 		/* Store now, print later: printk would stretch the windows */
// 		k_timer_start(&window_timer, K_MSEC(WINDOW_MS), K_MSEC(WINDOW_MS));
// 		for (int i = 0; i < N_WINDOWS; i++) {
// 			k_timer_status_sync(&window_timer);

// 			int32_t l = encoder_count(SIDE_LEFT);
// 			int32_t r = encoder_count(SIDE_RIGHT);

// 			d_left[i] = l - last_l;
// 			d_right[i] = r - last_r;
// 			last_l = l;
// 			last_r = r;
// 		}
// 		k_timer_stop(&window_timer);

// 		printk("duty %d:\n", duties[k]);
// 		stats("left", d_left);
// 		stats("right", d_right);
// 	}

// 	motor_brake();
// 	printk("done, missed edges: left %d  right %d\n",
// 	       encoder_errors(SIDE_LEFT), encoder_errors(SIDE_RIGHT));
// }

int main(void)
{
	if (blinker_init() < 0) {
		printk("blinker_init failed\n");
		return 0;
	}

	/* Brakes both motors, then sets up the encoders */
	if (drive_init() < 0) {
		printk("drive_init failed\n");
		return 0;
	}

	while (1) {
		printk("%d\n", encoder_count(SIDE_LEFT));
		printk("%d\n", encoder_count(SIDE_RIGHT));

		k_msleep(30);
	}

	return 0;
}
