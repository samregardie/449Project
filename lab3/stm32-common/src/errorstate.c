/*
 * Error state: hazards on, motors braked, commands ignored.
 *
 * One bit per cause, since each has its own exit rule. Outputs change only
 * when the mask goes between zero and non-zero, and are set under the lock
 * so the link timer ISR can't interleave. Lock order: errorstate, then
 * drive/blinker/servo.
 *
 * The link timer is not started at power-up; ERR_POWERUP covers that.
 */

#include <zephyr/kernel.h>

#include "blinker.h"
#include "drive.h"
#include "errorstate.h"
#include "servo.h"

static struct k_spinlock lock;
static uint32_t causes = ERR_POWERUP;	/* drive, blinker and servo also start in error */

static void update_error_causes(uint32_t set, uint32_t clear)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool was_error = causes != 0;

	causes = (causes | set) & ~clear;

	bool is_error = causes != 0;

	if (is_error != was_error) {
		drive_set_error(is_error);
		blinker_set_error(is_error);
		servo_set_error(is_error);
	}

	k_spin_unlock(&lock, key);
}

/* Runs in ISR context */
static void link_expired(struct k_timer *timer)
{
	update_error_causes(ERR_LINK_LOST, 0);
}

K_TIMER_DEFINE(link_timer, link_expired, NULL);

void errorstate_frame_ok(void)
{
	k_timer_start(&link_timer, K_MSEC(LINK_TIMEOUT_MS), K_NO_WAIT);
	update_error_causes(0, ERR_POWERUP | ERR_LINK_LOST | ERR_BAD_CMD);
}

void errorstate_out_of_range(void)
{
	update_error_causes(ERR_BAD_CMD, 0);
}

void errorstate_selftest_enter(void)
{
	update_error_causes(ERR_SELFTEST, 0);
}

void errorstate_selftest_exit(void)
{
	update_error_causes(0, ERR_SELFTEST);
}

uint32_t errorstate_causes(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	uint32_t c = causes;

	k_spin_unlock(&lock, key);
	return c;
}

bool errorstate_active(void)
{
	return errorstate_causes() != 0;
}
