/*
 * Keyboard drive test: the console stands in for the Pi, sending a frame
 * every 20 ms. Wheels off the ground.
 *   a/d  turn the wheel       q/e  left/right blinker
 *   w    throttle up          s    brake (throttle kept: brake wins)
 *   x    throttle down        t    self-test on/off
 *   p    pause frames (link loss) on/off
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#include "blinker.h"
#include "drive.h"
#include "errorstate.h"
#include "servo.h"

// TESTING ONLY - REMOVE
#define FRAME_MS 20	/* same as the Pi */
#define STEER_STEP 5	/* per key press */
#define STEER_RATE 1	/* per frame, so the "wheel" turns smoothly */
#define THROTTLE_STEP 10

static const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static int32_t steer_target;	/* where the keys want the wheel */
static int32_t steering;	/* the wheel, sent every frame */
static int32_t throttle;
static bool brake;
static bool selftest;
static bool paused;

static void print_status(void)
{
	struct drive_status d = drive_get_status();

	printk("wheel %4d  throttle %4d  brake %d  | target %5.1f  vel %5.1f  duty %5d"
	       "  | error 0x%x%s\n",
	       steering, throttle, brake, (double)d.target, (double)d.velocity, d.duty,
	       errorstate_causes(), paused ? "  (paused)" : "");
}

static void handle_key(char c)
{
	switch (c) {
	case 'a':
		steer_target = MAX(steer_target - STEER_STEP, -SERVO_STEER_MAX);
		break;
	case 'd':
		steer_target = MIN(steer_target + STEER_STEP, SERVO_STEER_MAX);
		break;
	case 'w':
		brake = false;
		throttle = MIN(throttle + THROTTLE_STEP, DRIVE_THROTTLE_MAX);
		break;
	case 'x':
		brake = false;
		throttle = MAX(throttle - THROTTLE_STEP, -DRIVE_THROTTLE_MAX);
		break;
	case 's':
		brake = true;
		break;
	case 'q':
		blinker_left_pressed();
		break;
	case 'e':
		blinker_right_pressed();
		break;
	case 't':
		selftest = !selftest;
		if (selftest) {
			errorstate_selftest_enter();
		} else {
			errorstate_selftest_exit();
		}
		break;
	case 'p':
		paused = !paused;
		break;
	default:
		return;
	}

	print_status();
}

int main(void)
{
	/* All start in the error state: hazards, braked, servo centred */
	if (blinker_init() < 0 || drive_init() < 0 || servo_init() < 0) {
		printk("init failed\n");
		return 0;
	}

	printk("a/d steer, w/x throttle, s brake, q/e blinkers, t self-test, p pause\n");

	for (int frame = 0;; frame++) {
		unsigned char c;

		while (uart_poll_in(console, &c) == 0) {
			handle_key(c);
		}

		steering += CLAMP(steer_target - steering, -STEER_RATE, STEER_RATE);

		/* Same order the parser will use: frame_ok first */
		if (!paused) {
			errorstate_frame_ok();
			drive_command(throttle, brake);
			servo_command(steering);
			blinker_steering(steering);
		}

		if (frame % (1000 / FRAME_MS) == 0) {
			print_status();
		}

		k_msleep(FRAME_MS);
	}

	return 0;
}
