#include <stdio.h>
#include <pico/stdlib.h>
#include <string.h>
#include <stdlib.h>

#include <bsp/board.h>
#include <tusb.h>
#include "usb.h"
#include "../config.h"

enum  {
	BLINK_NOT_MOUNTED = 250,
	BLINK_MOUNTED = 1000,
	BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

void cdc_task(void);
void led_blinking_task(void);
void SetDiskImages(DriveInterface *drives);

void InitUSB(DriveInterface *drives)
{
	SetDiskImages(drives);
	board_init();
	tusb_init();
}

void USB_main(void)
{
	while (true)
	{
		tud_task();
		led_blinking_task();
		cdc_task();
	}
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
	blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
	blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
	(void) remote_wakeup_en;
	blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
	blink_interval_ms = BLINK_MOUNTED;
}


//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
void cdc_task(void)
{
	if ( tud_cdc_connected() )
	{
		Serial::self->SendAndReceive();
		tud_cdc_write_flush();
	}
}

// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
	(void) itf;

	// connected
	if ( dtr && rts )
	{
		// print initial message when connected
		//tud_cdc_write_str("\r\nUSB started\r\n");

	}
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
	(void) itf;
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
	static uint32_t start_ms = 0;
	static bool led_state = false;

	// Blink every interval ms
	if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
	start_ms += blink_interval_ms;

	board_led_write(led_state);
	led_state = 1 - led_state; // toggle
}

Serial *Serial::self = NULL;

Serial::Serial(Config* config)
{
	self = this;
	m_s2u_len = 0;
    mutex_init(&m_mutex);
	
	Config::Com com = config->GetCom();
	uart_init(uart0, com.baud);
	uart_set_format(uart0, 8, com.stop, com.parity == 0 ? UART_PARITY_NONE : (com.parity == 1 ? UART_PARITY_ODD : UART_PARITY_EVEN));
	uart_set_hw_flow(uart0, true, true);	// We want hardware flow with the ST, as gdbserver uses it to detect break signal.
 
    // Set the GPIO pin mux to the UART - 0 is TX, 1 is RX
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    gpio_set_function(2, GPIO_FUNC_UART);
    gpio_set_function(3, GPIO_FUNC_UART);
	
}

Serial::~Serial(void)
{
}

#ifdef SERIAL_LOGGING
void Serial::WriteToUsb(const char* src)
{
    mutex_enter_blocking(&m_mutex);

	uint32_t len = 0;
	while (src[len] != 0 && m_s2u_len < SERIAL_BUFFER_LEN)
	{
		m_serialToUsb[m_s2u_len++] = src[len++];
	}
	
    mutex_exit(&m_mutex);
}

void Serial::WriteHexToUsb(uint32_t val, int len)
{
	char buf[10];
	const char hex[] = "0123456789abcdef";
	int shift = len * 4;
	for (int i = 0; i < len; ++i)
	{
		shift -= 4;
		buf[i] = hex[(val >> shift) & 0xf];
	}
	buf[len] = 0;
	WriteToUsb(buf);
}
#endif

void Serial::SendAndReceive(void)
{
    mutex_enter_blocking(&m_mutex);
	// from serial to usb.
	while (uart_is_readable(uart0) && tud_cdc_write_available() > 0)
	{
		char c = uart_getc(uart0);
		tud_cdc_write_char(c);
	}

	// Send to USB
	uint32_t avail = tud_cdc_write_available();
	uint32_t p = 0;
	if (avail != 0 && p != m_s2u_len)
	{
		while (avail != 0 && p != m_s2u_len)
		{
			tud_cdc_write_char(m_serialToUsb[p]);
			++p;
			--avail;
		}
		uint32_t i = 0;
		while ( p < m_s2u_len)
		{
			m_serialToUsb[i++] = m_serialToUsb[p++];
		}
		m_s2u_len = i;
	}
	
	// from USB to Serial
	while (uart_is_writable(uart0) && tud_cdc_available())
	{
		int32_t c = tud_cdc_read_char();
		uart_putc(uart0, (char)c);
	}
	
    mutex_exit(&m_mutex);
}
