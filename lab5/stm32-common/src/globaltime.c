/*
 * Global time (Lab 5, Part 6). The leader's REF carries its clock (L).
 *
 * At each REF:   clock_settime(L + REF_DELAY_US)
 * Between REFs:  global = clock + drift correction for the time since then
 * Drift ratio = leader time elapsed / local time elapsed, over ~1 s windows
 * (one 20 ms interval is too short: timestamp jitter would swamp it).
 */

#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/printk.h>

#include "canbus.h"
#include "globaltime.h"

/*
 * Leader reads its clock -> we timestamp the REF. ~270 us is the frame time
 * at 500 kbit/s. TODO (team): replace with the scope measurement from the
 * Pi's CAN_TX_A edge to our REF_RX edge.
 */
#define REF_DELAY_US 270

#define DRIFT_WINDOW_US 1000000
#define SYNC_LOST_MS (RAFT_MISSED_HEARTBEATS * RAFT_HEARTBEAT_MS)
#define PPS_HIGH_MS 100

/* Below the CAN and control threads (1-3): it only prints */
#define PPS_PRIO K_PRIO_PREEMPT(5)

#define USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec sync_led = GPIO_DT_SPEC_GET(USER_NODE, sync_gpios);
static const struct gpio_dt_spec ref_rx = GPIO_DT_SPEC_GET(USER_NODE, ref_rx_gpios);
static const struct gpio_dt_spec pps = GPIO_DT_SPEC_GET(USER_NODE, pps_gpios);

/* Shared by the CAN dispatch thread, timer ISRs and any global_gettime() caller */
static struct k_spinlock lock;
static bool synced;
static uint16_t ref_id;			/* which leader's REF we follow */
static int64_t set_at;			/* clock value at the last REF */
static int64_t step_us;			/* how far the last REF moved the clock */
static int64_t win_leader, win_local;	/* start of the current drift window */
static int64_t leader_span = 1;		/* last full window: ratio = leader_span */
static int64_t local_span = 1;		/*                          / local_span */

/* Local clock in us, never stepped */
static int64_t local_us(void)
{
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

/* Clock set by clock_settime(), in us */
static int64_t clock_us(void)
{
	struct timespec ts;

	sys_clock_gettime(SYS_CLOCK_REALTIME, &ts);
	return ts.tv_sec * USEC_PER_SEC + ts.tv_nsec / NSEC_PER_USEC;
}

/* Caller holds `lock` */
static int64_t global_locked(void)
{
	int64_t clock = clock_us();

	return clock + (clock - set_at) * (leader_span - local_span) / local_span;
}

int64_t global_gettime(void)
{
	int64_t t;

	K_SPINLOCK(&lock) {
		t = global_locked();
	}

	return t;
}

/* No REF for SYNC_LOST_MS: keep running on the last drift ratio */
static void sync_lost(struct k_timer *timer)
{
	K_SPINLOCK(&lock) {
		synced = false;
	}
	gpio_pin_set_dt(&sync_led, 0);
}

K_TIMER_DEFINE(sync_timer, sync_lost, NULL);

/* REF (catalog.h): seq, term, cycle, leader time in us (5 bytes, little-endian) */
static void on_ref(const struct can_frame *f)
{
	int64_t local = local_us();	/* first: any delay before this is sync error */
	int64_t leader = 0;

	gpio_pin_toggle_dt(&ref_rx);

	for (int i = 0; i < 5; i++) {
		leader |= (int64_t)f->data[3 + i] << (8 * i);
	}

	int64_t now = leader + REF_DELAY_US;
	struct timespec ts = { .tv_sec = now / USEC_PER_SEC,
			       .tv_nsec = (now % USEC_PER_SEC) * NSEC_PER_USEC };

	K_SPINLOCK(&lock) {
		if (!synced || f->id != ref_id) {
			/* First REF, after a loss, or a new leader: new window, ratio 1 */
			ref_id = f->id;
			win_leader = now;
			win_local = local;
			leader_span = local_span = 1;
		} else if (local - win_local >= DRIFT_WINDOW_US) {
			leader_span = now - win_leader;
			local_span = local - win_local;
			win_leader = now;
			win_local = local;
		}

		step_us = now - global_locked();
		sys_clock_settime(SYS_CLOCK_REALTIME, &ts);
		set_at = now;
		synced = true;
	}

	gpio_pin_set_dt(&sync_led, 1);
	k_timer_start(&sync_timer, K_MSEC(SYNC_LOST_MS), K_NO_WAIT);
}

/*
 * PPS: a timer ISR raises the pin on the whole global second, so the edge
 * doesn't wait for this low-priority thread; the thread prints and lowers it.
 */
static int64_t pps_global, pps_local;

K_SEM_DEFINE(pps_sem, 0, 1);

static void pps_rise(struct k_timer *timer)
{
	gpio_pin_set_dt(&pps, 1);
	pps_global = global_gettime();
	pps_local = local_us();
	k_sem_give(&pps_sem);
}

K_TIMER_DEFINE(pps_timer, pps_rise, NULL);

static void pps_loop(void *p1, void *p2, void *p3)
{
	while (1) {
		int64_t wait, ratio_ppm, step;
		bool was_synced;

		/* Time to the next whole second, converted to local time */
		K_SPINLOCK(&lock) {
			int64_t g = global_locked();

			wait = ((g / USEC_PER_SEC + 1) * USEC_PER_SEC - g) * local_span / leader_span;
		}
		k_timer_start(&pps_timer, K_USEC(wait), K_NO_WAIT);
		k_sem_take(&pps_sem, K_FOREVER);

		K_SPINLOCK(&lock) {
			ratio_ppm = leader_span * 1000000 / local_span;
			step = step_us;
			was_synced = synced;
		}

		printk("[N%d] PPS global=%d.%06d local=%d.%06d ratio=%d.%06d step=%dus%s\n",
		       NODE_ID, (int)(pps_global / USEC_PER_SEC), (int)(pps_global % USEC_PER_SEC),
		       (int)(pps_local / USEC_PER_SEC), (int)(pps_local % USEC_PER_SEC),
		       (int)(ratio_ppm / 1000000), (int)(ratio_ppm % 1000000), (int)step,
		       was_synced ? "" : " (not synced)");

		k_msleep(PPS_HIGH_MS);
		gpio_pin_set_dt(&pps, 0);
	}
}

K_THREAD_DEFINE(pps_thread, 1024, pps_loop, NULL, NULL, NULL, PPS_PRIO, 0, SYS_FOREVER_MS);

int globaltime_init(void)
{
	if (gpio_pin_configure_dt(&sync_led, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&ref_rx, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&pps, GPIO_OUTPUT_INACTIVE) != 0) {
		return -EIO;
	}

	/* Follow whichever node leads */
	for (int n = 1; n <= RAFT_N; n++) {
		if (canbus_on(CAN_ID_REF(n), on_ref) != 0) {
			return -ENOMEM;
		}
	}

	k_thread_start(pps_thread);
	return 0;
}
