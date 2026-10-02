/*
 * Setup UART with pi, recieve command packets, and send heartbeat.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>

#include "picom.h"

static const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

/* Test point: toggles each time a 0x7F ends a command frame */
static const struct gpio_dt_spec tp_cmd_rx =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), cmd_rx_gpios);

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
        if (c == 127) {
            /* 0x7F is never data: an empty frame (e.g. a doubled 0x7F) is dropped */
            if (rx_pos > 0) {
                gpio_pin_toggle_dt(&tp_cmd_rx);
                rx_buf[rx_pos] = 127;
                k_msgq_put(&rx_q, rx_buf, K_NO_WAIT);
                rx_pos = 0;
            }
        } else if (rx_pos < sizeof(rx_buf) - 1) {
            rx_buf[rx_pos++] = c;
        }
    }
}

void picom_send(const char *s)
{
    while (*s != 127) {
        uart_poll_out(uart, *s++);
    }
    uart_pull_out(uart, *s);
}

/* Blocks until the next complete message, then copies it to output. */
void picom_wait(char *output)
{
	k_msgq_get(&rx_q, output, K_FOREVER);
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
    if (!device_is_ready(uart) || !gpio_is_ready_dt(&tp_cmd_rx)) {
        return 1;
    }
    if (gpio_pin_configure_dt(&tp_cmd_rx, GPIO_OUTPUT_INACTIVE) < 0) {
        return 1;
    }
    uart_irq_callback_user_data_set(uart, uart_cb, NULL);
    uart_irq_rx_enable(uart);

    return 0;
}
