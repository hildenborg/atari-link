#include <stdio.h>
#include <pico/stdlib.h>
#include <string.h>
#include <hardware/gpio.h>
#include <bsp/board.h>
#include "acsi.h"
#include "acsi_asm.pio.h"
#include "image.h"
#include "Usb/usb.h"

// Acsi return codes
#define NO_ERROR 			0x000000
#define MEDIA_ERROR 		0x0b0800
#define WRITE_PROTECTED		0x072700
#define COMMAND_REJECTED 	0x0b670c
#define SEEK_ERROR 			0x0b1501
#define UNIT_ATTENTION 		0x062800


#define OUT_SIGNALS			20	// +3
#define DATA_BASE			4	// +12

// Pins
#define ACSI_ACK	DATA_BASE + 8
#define ACSI_CS		DATA_BASE + 9
#define ACSI_A1		DATA_BASE + 10
#define ACSI_RW		DATA_BASE + 11	// Unused for now, maybe forever.

#define ACSI_DRQ	OUT_SIGNALS
#define ACSI_IRQ	OUT_SIGNALS + 1
#define BUFFER_RW	OUT_SIGNALS + 2

#define ACSI_RST	26	// Atari reset line.
#define BUFFER_OE	27	// LOW will enable all io pins and ACK, CS, A1, RST.

#define DRQIRQ_OE	28	// LOW will enable IRQ and DRQ pins.

// State machine
#define PIO_ACSI	pio0
#define SM_ACSI	0

Acsi::Acsi(void)
	: m_errorCode(NO_ERROR)
{
	m_dataBuf = new uint8_t[LOGICAL_BLOCK * 255];
}

Acsi::~Acsi(void)
{
	Stop();
	delete[] m_dataBuf;
}

bool Acsi::Init(DriveInterface *drives, int32_t baseId)
{
	bool result = true;
	m_drives = drives;
	m_baseId = baseId;
	
	// Create a list of response lengths for the device + command combination.
	memset(m_fastResponse, 0, 256);
	uint32_t numLuns = m_drives->GetNumOfLuns();
	for (uint32_t i = baseId; i < (numLuns + baseId); ++i)
	{
		for (uint32_t j = 0; j <  0x1f; j++)
		{
			m_fastResponse[(i << 5) + j] = 6 - 2;	// 6 bytes - 1 we already got, and - 1 for loop. 
		}
		m_fastResponse[(i << 5) + 0x1f] = 11 - 2;	// 11 bytes icd - 1 we already got, and - 1 for loop. 
	}
	
    m_pio_offset = pio_add_program(PIO_ACSI, &AcsiInterface_program);
    AcsiInterface_program_init(PIO_ACSI, SM_ACSI, m_pio_offset, OUT_SIGNALS, DATA_BASE);

	gpio_init(ACSI_RST);
	gpio_set_dir(ACSI_RST, GPIO_IN);
	gpio_set_pulls(ACSI_RST, false, false);

	gpio_init(BUFFER_OE);
	gpio_set_dir(BUFFER_OE, GPIO_OUT);
	gpio_put(BUFFER_OE, false);

	gpio_init(DRQIRQ_OE);
	gpio_set_dir(DRQIRQ_OE, GPIO_OUT);
	gpio_put(DRQIRQ_OE, false);

	return result;
}

bool Acsi::GetWithTimeout(uint32_t timeout, uint32_t* data) const
{
	uint32_t timeEnd = board_millis() + timeout;
	while (pio_sm_is_rx_fifo_empty(PIO_ACSI, SM_ACSI))
	{
		if (timeEnd <= board_millis())
		{
			return false;
		}
	}
	*data = pio_sm_get_blocking(PIO_ACSI, SM_ACSI);
	return true;
}

void __not_in_flash_func(Acsi::ResetPio)(void)
{
	pio_sm_set_enabled(PIO_ACSI, SM_ACSI, false);
	PrintPioInfo();
	// Wait for reset and a1+cs to be set high.
	uint32_t mask = (1 << ACSI_RST) | (1 << ACSI_A1) | (1 << ACSI_CS);
	while((gpio_get_all() & mask) != mask)
	{
	}

	// Turn buffers off and on again.
	gpio_put(BUFFER_OE, true);
	gpio_put(DRQIRQ_OE, true);
	sleep_ms(1);
	gpio_put(BUFFER_OE, false);
	gpio_put(DRQIRQ_OE, false);

	pio_sm_clear_fifos(PIO_ACSI, SM_ACSI);
	pio_sm_restart(PIO_ACSI, SM_ACSI);
	pio_sm_exec(PIO_ACSI, SM_ACSI, pio_encode_jmp(m_pio_offset));
	pio_sm_set_enabled(PIO_ACSI, SM_ACSI, true);
}

void Acsi::ClearAllErrors(void)
{
	uint32_t numLuns = m_drives->GetNumOfLuns();
	for (uint32_t i = 0; i < numLuns; ++i)
	{
		m_drives->ClearError(i);
	}
}

// Look closer at PIO_ACSI->INPUT_SYNC_BYPASS,
// __time_critical_func
// https://github.com/raspberrypi/pico-examples/blob/master/gpio/hello_gpio_irq/hello_gpio_irq.c for rst
void __not_in_flash_func(Acsi::Start)(void)
{
	uint32_t cmdCounter = 0;	// Used for debugging.
	uint32_t firstByte;
	uint32_t cmdLen;
	uint32_t myRXFifoStatFlag = 1u << (PIO_FSTAT_RXEMPTY_LSB + SM_ACSI); 
	int32_t numLuns = (int32_t)m_drives->GetNumOfLuns();
	while (true)
	{
		ClearAllErrors();
		ResetPio();
		while (gpio_get(ACSI_RST))	// RST is active low.
		{
			// Pio code is at or soon to be at  AcsiInterface WaitForCmd
			while ((PIO_ACSI->fstat & myRXFifoStatFlag) == 0)
			{
				firstByte = PIO_ACSI->rxf[SM_ACSI];	// Ultra quick fifo get.
				if (firstByte > 255) 
				{
					// This sometimes happens on TOS1.06 and AHDI 6.061
					// It is often preceeded by a single command byte that times out. 
					Serial::self->WriteToUsb("Garbage!!!\r\n");
					PIO_ACSI->txf[SM_ACSI] = 0;
					continue;
				}
				PIO_ACSI->txf[SM_ACSI] = cmdLen = m_fastResponse[firstByte];	// and put

				if (cmdLen != 0)
				{
					cmdLen++;	// Add one byte that the pio loop delivers.
					// Fetch bytes from pio AcsiInterface CmdBytesLoop
					bool gotCmd = true;
					uint32_t readLen = 0;
					while (gotCmd && readLen < cmdLen)
					{
						gotCmd &= GetWithTimeout(10000, &m_cmdBuf[readLen >> 2]);
						readLen += 4;
					}
					if (!gotCmd)
					{
						Serial::self->WriteToUsb("Timeout or signal error, resetting!\r\n");
						ResetPio();
						continue;
					}
					
					m_device = (int32_t)((firstByte >> 5) & 0x7) - m_baseId;
					m_cmd = firstByte & 0x1f;
					while (!gpio_get(ACSI_CS))
					{
						// Just wait for CS to reset to high before continuing.
						// This would be nicer to have in pio code, but we ran out of instruction space.
					}
					
					// Adjust last command word
					uint32_t lword = (readLen >> 2) - 1;
					uint32_t lshift = readLen - cmdLen; // number of bytes not filled in last word
					m_cmdBuf[lword] = m_cmdBuf[lword] >> (lshift * 8);
					// Now handle the command.
					uint32_t statusByte = HardDiskCommand();
					pio_sm_put_blocking(PIO_ACSI, SM_ACSI, statusByte);
				}
			}
		}
		Serial::self->WriteToUsb("ATARI reset!\r\n");
	}
}

void Acsi::Stop(void)
{
}

int Acsi::SeekAddressGetNum(uint32_t *seekAddress) const
{
	*seekAddress = (((m_cmdBuf[0] >> 16) & 0xff) | (m_cmdBuf[0] & 0xff00) | ((m_cmdBuf[0] << 16) & 0xff0000));
	return ((m_cmdBuf[0] >> 24) & 0xff);
}

int Acsi::SeekAddressGetNum10(uint32_t *seekAddress) const
{
	// FatFs is limited to unsigned 32 bit, so we are too.
	// Meaning that for position, we can skip the msb
	*seekAddress = (((m_cmdBuf[0] >> 8) & 0xff0000) | ((m_cmdBuf[1] << 8) & 0xff00) | ((m_cmdBuf[1] >> 8) & 0xff));
	return (((m_cmdBuf[1] >> 16) & 0xff00) || (m_cmdBuf[2] & 0xff));
}

uint32_t Acsi::Command_03(void)
{
	int len = (m_cmdBuf[0] >> 24) & 0xff;
	
	for (int i = len; --i >= 0;)
	{
		m_dataBuf[i] = 0;
	}
	
	m_dataBuf[0] = 0x70;
	m_dataBuf[2] = (uint8_t)((m_errorCode >> 16) & 0xff);
	if (len >= 16)
	{
		len = 16;
		m_dataBuf[7] = (uint8_t)(len - 8);
		m_dataBuf[12] = (uint8_t)((m_errorCode >> 8) & 0xff);
		m_dataBuf[13] = (uint8_t)((m_errorCode) & 0xff);
	}

	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);	// Read 0 bytes from Atari
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, len);	// Write len bytes to Atari
	uint32_t* buf = (uint32_t*)m_dataBuf;
	for (int i = 0; i < (len >> 2); ++i)
	{
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, buf[i]);
	}
	m_drives->ClearError(m_device);
	return 0;	// Always succeed
}

uint32_t Acsi::Command_08(void)
{
	uint32_t statusByte = 2;
	uint32_t pos = 0;
	int num = SeekAddressGetNum(&pos);
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);	// Read 0 bytes from Atari
	num *= LOGICAL_BLOCK;
	if (m_drives->Read(m_device, pos, 0, num, m_dataBuf))
	{
		statusByte = 0;
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, num);	// Write num bytes to Atari
		uint32_t* buf = (uint32_t*)m_dataBuf;
		for (int i = 0; i < (num >> 2); ++i)
		{
			pio_sm_put_blocking(PIO_ACSI, SM_ACSI, buf[i]);
		}
	}
	else
	{
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Write 0 bytes to Atari
		if (m_drives->GetError(m_device) == NEED_UPDATE)
		{
			m_errorCode = UNIT_ATTENTION;
		}
		else
		{
			m_errorCode = MEDIA_ERROR;
		}
	}
	return statusByte;
}

// Write
uint32_t Acsi::Command_0A(void)
{
	uint32_t statusByte = 2;
	uint32_t pos = 0;
	int num = SeekAddressGetNum(&pos);

	if (m_drives->GetError(m_device) == NEED_UPDATE)
	{
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Read 0 bytes from Atari
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Write 0 bytes to Atari
		m_errorCode = UNIT_ATTENTION;
		return statusByte;
	}
	if (!m_drives->IsWriteable(m_device))
	{
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Read 0 bytes from Atari
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Write 0 bytes to Atari
		m_errorCode = WRITE_PROTECTED;
		return statusByte;
	}
	num *= LOGICAL_BLOCK;
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, num); // Read num bytes from Atari
	uint32_t* buf = (uint32_t*)m_dataBuf;
	for (int i = 0; i < (num >> 2); ++i)
	{
		buf[i] = pio_sm_get_blocking(PIO_ACSI, SM_ACSI);
	}
	if (m_drives->Write(m_device, pos, 0, num, m_dataBuf))
	{
		statusByte = 0;
	}
	else
	{
		m_errorCode = MEDIA_ERROR;
	}
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);	// Write 0 bytes to Atari
	return statusByte;
}

// Inquiry
uint32_t Acsi::Command_12(void)
{
	int len = (m_cmdBuf[0] >> 24) & 0xff;
	len = len <= 16 ? 16 : len & ~0xf;
	memset(m_dataBuf, 0, len);

// The line below didn't work as (at least AHDI) driver didn't recognize 5 as a read only device. However, returning a write protected error when writing did the same trick.
//	m_dataBuf[0] = m_drives->IsWriteable(m_device) ? 0x00 : 0x05; // 0 = HD, 5 = CD
	m_dataBuf[0] = 0x00;	// Always HD.
	m_dataBuf[1] = m_drives->IsRemovable(m_device) ? 0x80 : 0x00; // 80 = removable
	m_dataBuf[2] = m_drives->IsIcd(m_device) ? 0x02 : 0x01; // ACSI version (2 ICD)
	m_dataBuf[3] = m_drives->IsIcd(m_device) ? 0x02 : 0x00; // Response Data Format 2 = SCSI-3
	m_dataBuf[4] = (uint8_t)len - 5;

	m_drives->GetVendor8(m_device, (char*)(m_dataBuf + 8));
	m_drives->GetProduct16(m_device, (char*)(m_dataBuf + 16));
	m_drives->GetVersion4(m_device, (char*)(m_dataBuf + 32));

	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);	// Read 0 bytes from Atari
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, len);	// Write len bytes to Atari
	uint32_t* buf = (uint32_t*)m_dataBuf;
	for (int i = 0; i < (len >> 2); ++i)
	{
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, buf[i]);
	}
	return 0;	// Always succeed
}

//	ICD Disk capacity
uint32_t Acsi::Command_25(void)
{
	uint32_t blocks = m_drives->GetLSectorCount(m_device);
	m_dataBuf[0] = (uint8_t)(blocks >> 24);
	m_dataBuf[1] = (uint8_t)(blocks >> 16);
	m_dataBuf[2] = (uint8_t)(blocks >> 8);
	m_dataBuf[3] = (uint8_t)(blocks);
	m_dataBuf[4] = 0;
	m_dataBuf[5] = 0;
	m_dataBuf[6] = (uint8_t)(LOGICAL_BLOCK >> 8);
	m_dataBuf[7] = 0;
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);	// Read 0 bytes from Atari
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 8);	// Write 8 bytes to Atari
	uint32_t* buf = (uint32_t*)m_dataBuf;
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, buf[0]);
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, buf[1]);
	return 0;	// Always succeed
}

//	ICD Read10
uint32_t Acsi::Command_28(void)
{
	uint32_t statusByte = 2;
	size_t size = m_drives->GetLSectorCount(m_device);
	uint32_t pos = 0;
	int num = SeekAddressGetNum10(&pos);
	if ((size - pos) < num)
	{
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Read 0 bytes from Atari
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Write 0 bytes to Atari
		m_errorCode = MEDIA_ERROR;
		return statusByte;
	}

	if (m_drives->GetError(m_device) == NEED_UPDATE)
	{
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Read 0 bytes from Atari
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Write 0 bytes to Atari
		m_errorCode = UNIT_ATTENTION;
		return statusByte;
	}
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);	// Read 0 bytes from Atari
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, num);	// Write num bytes to Atari
	statusByte = 0;	// This is fine...

	uint32_t endpos = pos + num;
	#define BURSTLEN 255
	uint32_t* buf = (uint32_t*)m_dataBuf;
	while (pos < endpos)
	{
		uint32_t burst = endpos - pos;
		if (burst > BURSTLEN)
		{
			burst = BURSTLEN;
		}
		m_drives->Read(m_device, pos, 0, burst * LOGICAL_BLOCK, m_dataBuf);
		for (int i = 0; i < (burst >> 2); ++i)
		{
			pio_sm_put_blocking(PIO_ACSI, SM_ACSI, buf[i]);
		}
		pos += BURSTLEN;
	}
	#undef BURSTLEN
	return statusByte;
}

//	ICD Write10
uint32_t Acsi::Command_2a(void)
{
	uint32_t statusByte = 2;
	size_t size = m_drives->GetLSectorCount(m_device);
	uint32_t pos = 0;
	int num = SeekAddressGetNum10(&pos);
	if ((size - pos) < num)
	{
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Read 0 bytes from Atari
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Write 0 bytes to Atari
		m_errorCode = MEDIA_ERROR;
		return statusByte;
	}

	if (m_drives->GetError(m_device) == NEED_UPDATE)
	{
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Read 0 bytes from Atari
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Write 0 bytes to Atari
		m_errorCode = UNIT_ATTENTION;
		return statusByte;
	}
	if (!m_drives->IsWriteable(m_device))
	{
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Read 0 bytes from Atari
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0); // Write 0 bytes to Atari
		m_errorCode = WRITE_PROTECTED;
		return statusByte;
	}

	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, num); // Read num bytes from Atari
	statusByte = 0;	// This is fine...

	uint32_t endpos = pos + num;
	#define BURSTLEN 255
	uint32_t* buf = (uint32_t*)m_dataBuf;
	while (pos < endpos)
	{
		uint32_t burst = endpos - pos;
		if (burst > BURSTLEN)
		{
			burst = BURSTLEN;
		}
		for (int i = 0; i < (burst >> 2); ++i)
		{
			buf[i] = pio_sm_get_blocking(PIO_ACSI, SM_ACSI);
		}
		m_drives->Write(m_device, pos, 0, burst * LOGICAL_BLOCK, m_dataBuf);
		pos += BURSTLEN;
	}
	#undef BURSTLEN
	pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);	// Write 0 bytes to Atari
	return statusByte;
}

// ICD extended command
uint32_t Acsi::Command_1f(void)
{
	uint32_t statusByte = 2;
	uint32_t icdCmd = m_cmdBuf[0] & 0xff;
	switch (icdCmd)
	{
	case 0x25:
		//	Disk capacity
		statusByte = Command_25();
		break;
	case 0x28:
		//	Read10
		statusByte = Command_28();
		break;
	case 0x2a:
		//	Write10
		statusByte = Command_2a();
		break;
	default:
		// Unsupported
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);
		m_errorCode = COMMAND_REJECTED;
		break;
	}
	return statusByte;
}		

uint32_t Acsi::HardDiskCommand(void)
{
	uint32_t statusByte = 2;
	Serial::self->WriteToUsb("CMD bytes: 0x");
	Serial::self->WriteHexToUsb((m_device << 5) | m_cmd, 2);
	Serial::self->WriteHexToUsb(m_cmdBuf[0], 2);
	Serial::self->WriteHexToUsb(m_cmdBuf[0] >> 8, 2);
	Serial::self->WriteHexToUsb(m_cmdBuf[0] >> 16, 2);
	Serial::self->WriteHexToUsb(m_cmdBuf[0] >> 24, 2);
	Serial::self->WriteHexToUsb(m_cmdBuf[1], 2);
	if (m_cmd != 0x03)
	{
		m_errorCode = NO_ERROR;		
	}
	switch (m_cmd)
	{
	case 0x03:
		// Request sense
		statusByte = Command_03();
		break;
	case 0x00:
	case 0x04:
	case 0x0b:
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);
		if (m_drives->GetError(m_device) == NEED_UPDATE)
		{
			m_errorCode = UNIT_ATTENTION;
		}
		else
		{
			statusByte = 0;
		}
		break;
	case 0x08:
		// Read
		statusByte = Command_08();
		break;
	case 0x0a:
		// Write
		statusByte = Command_0A();
		break;
	case 0x12:
		// Inquiry
		statusByte = Command_12();
		break;
	case 0x1f:
		// ICD extended command
		statusByte = Command_1f();
		break;
	default:
		// Unsupported
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);
		pio_sm_put_blocking(PIO_ACSI, SM_ACSI, 0);
		m_errorCode = COMMAND_REJECTED;
		break;
	}
	Serial::self->WriteToUsb(" -> ");
	Serial::self->WriteHexToUsb(statusByte, 1);
	if (statusByte != 0)
	{
		Serial::self->WriteToUsb(" : ");
		Serial::self->WriteHexToUsb(m_errorCode, 6);
	}
	Serial::self->WriteToUsb("\r\n");
	return statusByte;
} 

void Acsi::PrintNameNumLn(const char* name, uint32_t num, int len)
{
	Serial::self->WriteToUsb(name);
	Serial::self->WriteToUsb(": ");
	Serial::self->WriteHexToUsb(num, len);
	Serial::self->WriteToUsb("\r\n");
}

// Calling this will destroy the current state.
// Look at rp2040-datasheet.pdf: 3.7. List of Registers and print more.
void Acsi::PrintPioInfo(void)
{
	uint8_t pc = pio_sm_get_pc(PIO_ACSI, SM_ACSI);
	uint32_t rxLevel = pio_sm_get_rx_fifo_level(PIO_ACSI, SM_ACSI);
	uint32_t txLevel = pio_sm_get_tx_fifo_level(PIO_ACSI, SM_ACSI);
	// Get isr count
	uint32_t isrCount = 0;
	uint32_t inNull1 = pio_encode_in(pio_null, 1);
	while (rxLevel == pio_sm_get_rx_fifo_level(PIO_ACSI, SM_ACSI))
	{
		pio_sm_exec_wait_blocking(PIO_ACSI, SM_ACSI, inNull1);
		isrCount++;
	}
	// Get osr count
	uint32_t osrCount = 0;
	uint32_t outNull1 = pio_encode_out(pio_null, 1);
	while (txLevel == pio_sm_get_tx_fifo_level(PIO_ACSI, SM_ACSI))
	{
		pio_sm_exec(PIO_ACSI, SM_ACSI, outNull1);
		if (pio_sm_is_exec_stalled(PIO_ACSI, SM_ACSI)) {break;}
		osrCount++;
	}
	PrintNameNumLn("PC", pc, 2);
	PrintNameNumLn("rxLevel", rxLevel, 2);
	PrintNameNumLn("txLevel", txLevel, 2);
	PrintNameNumLn("isrCount", isrCount, 2);
	PrintNameNumLn("osrCount", osrCount, 2);
}
