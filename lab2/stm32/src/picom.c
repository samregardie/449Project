/*
 * Setup UART with pi, recieve command packets, and send heartbeat.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "picom.h"

static const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

K_MSGQ_DEFINE(rx_q, 64, 4, 4);   /* queue of complete lines */
static char rx_buf[64];
static int rx_pos;


/*
 * UART recieve interrupt handler
 */
static void uart_cb(const struct device *dev, void *user_data)
{
    uint8_t c;

    uart_irq_update(dev);

    if (!uart_irq_rx_ready(dev)) {
        return;
    }
    while (uart_fifo_read(dev, &c, 1) == 1) {
        if ((c == '\n' || c == '\r') && rx_pos > 0) {
            rx_buf[rx_pos] = '\0';
            k_msgq_put(&rx_q, rx_buf, K_NO_WAIT);
            rx_pos = 0;
        } else if (rx_pos < sizeof(rx_buf) - 1) {
            rx_buf[rx_pos++] = c;
        }
    }
}

void picom_send(const char *s)
{
    while (*s) {
        uart_poll_out(uart, *s++);
    }
}

/* Reads message from uart buffer. Returns if data was read. */
bool picom_read(char *output){
	uint8_t latest[64];
	uint8_t tmp[64];
	bool got_one = false;

	while (k_msgq_get(&rx_q, tmp, K_NO_WAIT) == 0) {
	    	memcpy(latest, tmp, sizeof(latest));
	    	got_one = true;
	}

	if (got_one) {
		memcpy(output, latest, sizeof(latest));
	}	
	return got_one;
}

int picom_init(void)
{
    if (!device_is_ready(uart)) {
        return 1;
    }
    uart_irq_callback_user_data_set(uart, uart_cb, NULL);
    uart_irq_rx_enable(uart);

    return 0;
}
