/*
 * Peer-to-peer CAN test (no loopback). Flash the same build to both boards.
 *
 * Every 500 ms, each board sends a counter on bus A and bus B, under an ID
 * (0x700-0x7FF) derived from its chip serial so the two boards differ.
 * Byte 1 says which bus it was sent on. Every received frame is printed.
 *
 * Working: each board prints the other's ID on both A and B.
 * No peer on a bus: nobody ACKs, the 3 TX buffers fill, and sends on that
 * bus start failing with -EAGAIN.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/printk.h>

CAN_MSGQ_DEFINE(rx_a, 8);
CAN_MSGQ_DEFINE(rx_b, 8);

static const struct device *const can[] = {
	DEVICE_DT_GET(DT_ALIAS(can_a)),
	DEVICE_DT_GET(DT_ALIAS(can_b)),
};
static struct k_msgq *const rx[] = { &rx_a, &rx_b };
static const char name[] = { 'A', 'B' };

/* Every standard ID */
static const struct can_filter all = { .id = 0, .mask = 0 };

/* A callback makes can_send() return right away instead of waiting for an ACK */
static void tx_done(const struct device *dev, int error, void *user_data)
{
}

static uint16_t my_id(void)
{
	uint8_t uid[12];
	ssize_t len = hwinfo_get_device_id(uid, sizeof(uid));
	uint8_t hash = 0;

	for (int i = 0; i < len; i++) {
		hash ^= uid[i];
	}

	return 0x700 | hash;
}

int main(void)
{
	uint16_t id = my_id();

	printk("CAN peer test: sending id 0x%03x on A and B\n", id);

	for (int i = 0; i < 2; i++) {
		if (!device_is_ready(can[i]) ||
		    can_add_rx_filter_msgq(can[i], rx[i], &all) < 0 ||
		    can_start(can[i]) < 0) {
			printk("bus %c: init failed\n", name[i]);
			return 1;
		}
	}

	for (uint8_t counter = 0;; counter++) {
		for (int i = 0; i < 2; i++) {
			struct can_frame f = { .id = id, .dlc = 2, .data = { counter, name[i] } };
			int ret = can_send(can[i], &f, K_NO_WAIT, tx_done, NULL);

			if (ret < 0) {
				printk("bus %c: send failed (%d)\n", name[i], ret);
			}
		}

		k_sleep(K_MSEC(500));

		struct can_frame f;

		for (int i = 0; i < 2; i++) {
			while (k_msgq_get(rx[i], &f, K_NO_WAIT) == 0) {
				printk("bus %c: got id 0x%03x count %u sent on %c\n",
				       name[i], f.id, f.data[0], f.data[1]);
			}
		}
	}
}
