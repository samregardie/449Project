/*
 * errorstate bench test: fakes the parser and steps through every cause.
 * Watch the hazards and DIR_A; each step prints what you should see.
 * Wheels off the ground: the motors spin whenever the car is not in error.
 */

#include <zephyr/kernel.h>

#include "blinker.h"
#include "drive.h"
#include "errorstate.h"

// TESTING ONLY - REMOVE
#define TEST_THROTTLE 300
#define CMD_PERIOD_MS 20	/* same as the Pi */
#define STEP_MS 3000

static int64_t last_frame_ms;

/* Pretend to be the Pi: a valid frame every CMD_PERIOD_MS */
static void send_frames(int ms)
{
	for (int t = 0; t < ms; t += CMD_PERIOD_MS) {
		errorstate_frame_ok();
		last_frame_ms = k_uptime_get();
		drive_command(TEST_THROTTLE, false);
		k_msleep(CMD_PERIOD_MS);
	}
}

static void check(const char *step, uint32_t expected)
{
	uint32_t causes = errorstate_causes();

	printk("%-40s causes=0x%x expected=0x%x %s\n", step, causes, expected,
	       causes == expected ? "PASS" : "FAIL");
}

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

	// TESTING ONLY - REMOVE
	check("power-up: hazards, braked", ERR_POWERUP);
	k_msleep(STEP_MS);

	send_frames(STEP_MS);
	check("frames: normal, spinning", 0);

	errorstate_selftest_enter();
	send_frames(STEP_MS);
	check("self-test + frames: hazards, braked", ERR_SELFTEST);

	errorstate_selftest_exit();
	send_frames(STEP_MS);
	check("double press: normal, spinning", 0);

	errorstate_out_of_range();
	check("out of range: hazards, braked", ERR_BAD_CMD);
	k_msleep(STEP_MS / 2);
	send_frames(STEP_MS);
	check("next valid frame: normal, spinning", 0);

	/* Stop sending; time from the last frame to the fail-safe */
	while (!errorstate_active() && k_uptime_get() - last_frame_ms < 1000) {
		k_msleep(1);
	}
	printk("link lost %d ms after last frame (timeout %d, limit 100)\n",
	       (int)(k_uptime_get() - last_frame_ms), LINK_TIMEOUT_MS);
	check("link lost: hazards, braked", ERR_LINK_LOST);
	k_msleep(STEP_MS);

	send_frames(STEP_MS);
	check("frames again: normal, spinning", 0);

	/* Self-test and link loss together: only both exits clear it */
	errorstate_selftest_enter();
	k_msleep(STEP_MS);
	check("self-test + link lost", ERR_SELFTEST | ERR_LINK_LOST);
	send_frames(STEP_MS);
	check("frames: still self-test", ERR_SELFTEST);
	errorstate_selftest_exit();
	send_frames(STEP_MS);
	check("double press: normal, spinning", 0);

	printk("done; stopping frames -> link lost\n");
	return 0;
}
