#include "usb/serial.h"
#include <pico/stdlib.h>
#include <tusb.h>
#include "config/config.h"

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
