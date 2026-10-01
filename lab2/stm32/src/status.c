/*
 * Status frame to the Pi every 20 ms:
 *
 *   [I_left][I_right][I_servo][error causes][0x7F]
 *
 * Currents are int8, scaled to -100..100 and clamped. The error mask is 0..15
 * (ERR_* bits). So no data byte can be 0x7F, which stays the frame terminator.
 *
 * This thread is the only sender on the Pi link, so frames can't interleave.
 */

#include <zephyr/kernel.h>

#include "current.h"
#include "errorstate.h"
#include "picom.h"
#include "status.h"

int8_t status_scale_current(int ma)
{
	return CLAMP(ma * 100 / CURRENT_FULL_SCALE_MA, -100, 100);
}

K_SEM_DEFINE(status_start_sem, 0, 1);
K_TIMER_DEFINE(status_timer, NULL, NULL);

static void status_loop(void *p1, void *p2, void *p3)
{
	/* Wait until the ADC and UART are set up */
	k_sem_take(&status_start_sem, K_FOREVER);
	k_timer_start(&status_timer, K_MSEC(STATUS_PERIOD_MS), K_MSEC(STATUS_PERIOD_MS));

	while (1) {
		/* Periodic timer, so the period doesn't drift by the loop's run time */
		k_timer_status_sync(&status_timer);

		char frame[] = {
			status_scale_current(get_current(MOTOR_LEFT)),
			status_scale_current(get_current(MOTOR_RIGHT)),
			status_scale_current(get_current(SERVO)),
			(char)errorstate_causes(),
			0x7F,
		};

		picom_send(frame);
	}
}

/* Lowest of the app threads: parser 1, drive 2, status 3 */
K_THREAD_DEFINE(status_thread, 1024, status_loop, NULL, NULL, NULL, K_PRIO_PREEMPT(3), 0, 0);

int status_init(void)
{
	k_sem_give(&status_start_sem);
	return 0;
}
