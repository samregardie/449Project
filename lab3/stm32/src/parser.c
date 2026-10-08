/*
 * Command frames from the Pi:
 *
 *   [steer][throttle][brake][left][right][error]['\n'][0x7F]
 *
 * steer -100..100, throttle and brake 0..100, all int8. No reverse for now,
 * so a negative throttle is out of range. left/right/error are 0 or 1: whether
 * that wheel button is held (left paddle, right paddle, B = self-test).
 *
 * Buttons act on the press edge (0 -> 1 between valid frames). The wheel
 * reports them over USB already debounced, and we only sample once per frame
 * (20 ms), so no extra debounce here.
 *
 * Wrong length: malformed, dropped. Right length but out of range: error state.
 * The UART ISR splits frames on 0x7F, so one bad frame never affects the next.
 */

#include <zephyr/kernel.h>

#include "blinker.h"
#include "drive.h"
#include "errorstate.h"
#include "picom.h"
#include "servo.h"

#define FRAME_LEN 7		/* bytes before the 0x7F, including the '\n' */
#define BRAKE_MAX 100
#define BRAKE_THRESHOLD 10	/* brake above 10% counts as pressed */
#define DOUBLE_PRESS_MS 500	/* second self-test press within this exits */

/* Button levels in the last valid frame. Parser thread only. */
static bool prev_left;
static bool prev_right;
static bool prev_test;

/* Time of the last self-test press that could start a double, or -1 */
static int64_t last_test_press = -1;

static bool in_range(int32_t v, int32_t min, int32_t max)
{
	return v >= min && v <= max;
}

/* Single press: error state. Double press: leave it. */
static void selftest_pressed(void)
{
	int64_t now = k_uptime_get();

	if (last_test_press >= 0 && now - last_test_press <= DOUBLE_PRESS_MS) {
		errorstate_selftest_exit();
		last_test_press = -1;	/* so a third press starts over */
	} else {
		errorstate_selftest_enter();
		last_test_press = now;
	}
}

static void parser_loop(void *p1, void *p2, void *p3)
{
	char frame[64];

	/*
	 * No start semaphore: frames only arrive after picom_init(), which
	 * main() calls after drive, servo and blinker are set up.
	 */
	while (1) {
		/* Block, so each frame is handled as soon as it arrives */
		picom_wait(frame);

		int len = 0;

		while (len < sizeof(frame) && frame[len] != 0x7F) {
			len++;
		}

		if (len != FRAME_LEN || frame[FRAME_LEN - 1] != '\n') {
			continue;
		}

		int8_t steer = (int8_t)frame[0];
		int8_t throttle = (int8_t)frame[1];
		int8_t brake = (int8_t)frame[2];
		uint8_t left = frame[3];
		uint8_t right = frame[4];
		uint8_t test = frame[5];

		if (!in_range(steer, -SERVO_STEER_MAX, SERVO_STEER_MAX) ||
		    !in_range(throttle, 0, DRIVE_THROTTLE_MAX) ||
		    !in_range(brake, 0, BRAKE_MAX) ||
		    left > 1 || right > 1 || test > 1) {
			errorstate_out_of_range();
			continue;
		}

		/* First, so the outputs have left the error state */
		errorstate_frame_ok();

		/* Before drive/servo, so a press brakes in this same frame */
		if (test && !prev_test) {
			selftest_pressed();
		}

		drive_command(throttle, brake > BRAKE_THRESHOLD);
		servo_command(steer);

		/* Ignored by blinker.c while in hazards */
		if (left && !prev_left) {
			blinker_left_pressed();
		}
		if (right && !prev_right) {
			blinker_right_pressed();
		}
		blinker_steering(steer);

		prev_left = left;
		prev_right = right;
		prev_test = test;
	}
}

/* Highest of the app threads: parser 1, drive 2, status 3 */
K_THREAD_DEFINE(parser_thread, 1024, parser_loop, NULL, NULL, NULL, K_PRIO_PREEMPT(1), 0, 0);
