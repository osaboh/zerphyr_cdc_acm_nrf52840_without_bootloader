/*
    Copyright 2019 Joel Svensson	svenssonjoel@yahoo.se

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <device.h>
#include <drivers/uart.h>
#include <zephyr.h>
#include <sys/ring_buffer.h>

#include <stdio.h>

#define RING_BUF_SIZE 1024
u8_t in_ring_buffer[RING_BUF_SIZE];
u8_t out_ring_buffer[RING_BUF_SIZE];

struct device *dev;

struct ring_buf in_ringbuf;
struct ring_buf out_ringbuf;

static void interrupt_handler(struct device *dev)
{
	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			int recv_len, rb_len;
			u8_t buffer[64];
			size_t len = MIN(ring_buf_space_get(&in_ringbuf),
							 sizeof(buffer));

			recv_len = uart_fifo_read(dev, buffer, len);

			rb_len = ring_buf_put(&in_ringbuf, buffer, recv_len);
			if (rb_len < recv_len) {
				//silently dropping bytes
			}
		}

		if (uart_irq_tx_ready(dev)) {
			u8_t buffer[64];
			int rb_len, send_len;

			rb_len = ring_buf_get(&out_ringbuf, buffer, sizeof(buffer));
			if (!rb_len) {
				uart_irq_tx_disable(dev);
				continue;
			}

			send_len = uart_fifo_fill(dev, buffer, rb_len);
			if (send_len < rb_len) {
			}
		}
	}
}

int get_char() {

	int n;
	u8_t c;
	unsigned int key = irq_lock();
	n = ring_buf_get(&in_ringbuf, &c, 1);
	irq_unlock(key);
	if (n == 1) {
		return c;
	}
	return -1;
}

void put_char(int i) {
	if (i >= 0 && i < 256) {

		u8_t c = (u8_t)i;
		unsigned int key = irq_lock();
		ring_buf_put(&out_ringbuf, &c, 1);
		uart_irq_tx_enable(dev);
		irq_unlock(key);
	}
}

void usb_printf(char *format, ...) {

	va_list arg;
	va_start(arg, format);
	int len;
	static char print_buffer[4096];

	len = vsnprintf(print_buffer, 4096,format, arg);
	va_end(arg);

	int num_written = 0;
	while (len - num_written > 0) {
		unsigned int key = irq_lock();
		num_written +=
			ring_buf_put(&out_ringbuf,
						 (print_buffer + num_written),
						 (len - num_written));
		irq_unlock(key);
		uart_irq_tx_enable(dev);
	}
}


int inputline(char *buffer, int size) {
	int n = 0;
	int c;
	for (n = 0; n < size - 1; n++) {

		c = get_char();
		switch (c) {
		case 127: /* fall through to below */
		case '\b': /* backspace character received */
			if (n > 0)
				n--;
			buffer[n] = 0;
			put_char('\b'); /* output backspace character */
			n--; /* set up next iteration to deal with preceding char location */
			break;
		case '\n': /* fall through to \r */
		case '\r':
			buffer[n] = 0;
			return n;
		default:
			if (c != -1 && c < 256) {
				put_char(c);
				buffer[n] = c;
			} else {
				n --;
			}

			break;
		}
	}
	buffer[size - 1] = 0;
	return 0; // Filled up buffer without reading a linebreak
}

void main(void)
{

	u32_t baudrate, dtr = 0U;

	dev = device_get_binding("CDC_ACM_0");
	if (!dev) {
		return;
	}

	ring_buf_init(&in_ringbuf, sizeof(in_ring_buffer), in_ring_buffer);
	ring_buf_init(&out_ringbuf, sizeof(out_ring_buffer), out_ring_buffer);

	while (true) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			break;
		} else {
			k_sleep(100);
		}
	}

	uart_line_ctrl_set(dev, UART_LINE_CTRL_DCD, 1);
	uart_line_ctrl_set(dev, UART_LINE_CTRL_DSR, 1);

	k_busy_wait(1000000);

	uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);

	uart_irq_callback_set(dev, interrupt_handler);

	uart_irq_rx_enable(dev);

	usb_printf("Allocating input/output buffers\n\r");

	while (1) {
		usb_printf("Lisp REPL started (ZephyrOS)!\n\r");
		k_sleep(500);
	}

}
