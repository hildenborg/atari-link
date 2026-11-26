#include "scsi/scsi.h"
#include <string.h>
#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include "scsi_bus.pio.h"
#include "board/pins.h"
#include "drive/drive_interface.h"
#include "drive/logical_block.h"
#include "usb/serial.h"

// Sense: key << 16 | additional sense code << 8 | qualifier
#define SENSE_NONE				0x000000
#define SENSE_READ_ERROR		0x031100
#define SENSE_WRITE_ERROR		0x030c00
#define SENSE_ILLEGAL_OPCODE	0x052000
#define SENSE_LBA_OUT_OF_RANGE	0x052100
#define SENSE_INVALID_FIELD		0x052400
#define SENSE_LUN_NOT_SUPPORTED	0x052500
#define SENSE_MEDIUM_CHANGED	0x062800
#define SENSE_WRITE_PROTECTED	0x072700

// Status
#define STATUS_GOOD				0x00
#define STATUS_CHECK_CONDITION	0x02

// Messages
#define MSG_COMMAND_COMPLETE	0x00
#define MSG_EXTENDED			0x01
#define MSG_ABORT				0x06
#define MSG_REJECT				0x07
#define MSG_NOP					0x08
#define MSG_BUS_DEVICE_RESET	0x0c
#define MSG_IDENTIFY			0x80

// Sectors per DMA chunk. Two chunk buffers are used, so SD card access
// for one chunk overlaps the SCSI transfer of the other.
#define CHUNK_SECTORS	32
#define CHUNK_BYTES		(CHUNK_SECTORS * LOGICAL_BLOCK)

// The initiator must make progress within this time, or the connection is dropped.
#define TIMEOUT_US		2000000

// The initiator (the Atari) normally has id 7.
#define INITIATOR_ID	7

#define DATA_MASK		(0x1ffu << PIN_SCSI_DB0)	// DB0-DB7 and DBP

static_assert(PIN_SCSI_ACK == ACK_PIN, "ACK pin must be the same in scsi_bus.pio and pins.h");
static_assert(PIN_SCSI_DBP == PIN_SCSI_DB0 + 8, "DBP must follow DB7");
static_assert(PIN_SCSI_REQ < 32 && PIN_SCSI_DBP < 32 && PIN_SCSI_ACK < 32, "PIO pins must be below 32");

namespace
{
	inline uint32_t GetBE32(const uint8_t* p)
	{
		return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
	}

	inline uint32_t GetBE16(const uint8_t* p)
	{
		return ((uint32_t)p[0] << 8) | p[1];
	}

	inline void SetBE32(uint8_t* p, uint32_t v)
	{
		p[0] = (uint8_t)(v >> 24);
		p[1] = (uint8_t)(v >> 16);
		p[2] = (uint8_t)(v >> 8);
		p[3] = (uint8_t)v;
	}

	// Signals to the 74LVT125 are active low: putting 0 asserts the bus line.
	inline void Assert(uint pin, bool asserted)
	{
		gpio_put(pin, !asserted);
	}
};

Scsi::Scsi(void)
	: m_drives(NULL)
	, m_baseId(0)
	, m_idMask(0)
	, m_device(0)
	, m_lun(0)
	, m_reject(false)
	, m_aborted(false)
	, m_pio(pio0)
	, m_dmaChan(-1)
	, m_settleCycles(0)
{
	memset(m_sense, 0, sizeof(m_sense));
	for (int i = 0; i < 2; ++i)
	{
		m_buf[i] = new uint8_t[CHUNK_BYTES];
		m_enc[i] = new uint16_t[CHUNK_BYTES];
	}
	// SCSI uses odd parity: the number of asserted lines, DBP included, is odd.
	for (int b = 0; b < 256; ++b)
	{
		int bits = __builtin_popcount(b);
		m_parity[b] = (uint16_t)(b | ((bits & 1) == 0 ? 0x100 : 0));
	}
}

Scsi::~Scsi(void)
{
}

bool Scsi::Init(DriveInterface *drives, int32_t baseId)
{
	m_drives = drives;
	m_baseId = baseId;
	m_idMask = 0;
	int32_t numLuns = (int32_t)m_drives->GetNumOfLuns();
	for (int32_t i = 0; i < numLuns; ++i)
	{
		int32_t id = baseId + i;
		if (id >= 0 && id < INITIATOR_ID)
		{
			m_idMask |= (uint8_t)(1 << id);
		}
	}

	m_settleCycles = clock_get_hz(clk_sys) / 2500000 + 1;	// 400ns

	// Bus control outputs, released (high).
	const uint outputs[] = {PIN_SCSI_BSY_OUT, PIN_SCSI_MSG, PIN_SCSI_CD, PIN_SCSI_IO, PIN_SCSI_DATA_OE};
	for (uint pin : outputs)
	{
		gpio_init(pin);
		gpio_put(pin, true);
		gpio_set_dir(pin, GPIO_OUT);
		gpio_disable_pulls(pin);
	}
	gpio_init(PIN_SCSI_DATA_DIR);
	gpio_put(PIN_SCSI_DATA_DIR, false);	// Read the bus.
	gpio_set_dir(PIN_SCSI_DATA_DIR, GPIO_OUT);
	gpio_disable_pulls(PIN_SCSI_DATA_DIR);

	// Bus inputs, driven by the 74LVT245.
	const uint inputs[] = {PIN_SCSI_ACK, PIN_SCSI_ATN, PIN_SCSI_SEL, PIN_SCSI_RST, PIN_SCSI_BSY_IN, PIN_SCSI_DBP_IN};
	for (uint pin : inputs)
	{
		gpio_init(pin);
		gpio_set_dir(pin, GPIO_IN);
		gpio_disable_pulls(pin);
	}

	// Data and REQ belong to the pio.
	m_offsetTx = pio_add_program(m_pio, &scsi_tx_program);
	m_offsetRx = pio_add_program(m_pio, &scsi_rx_program);
	m_smTx = (uint)pio_claim_unused_sm(m_pio, true);
	m_smRx = (uint)pio_claim_unused_sm(m_pio, true);
	for (uint pin = PIN_SCSI_DB0; pin <= PIN_SCSI_DBP; ++pin)
	{
		pio_gpio_init(m_pio, pin);
		gpio_disable_pulls(pin);			// DBP has an external pullup.
		gpio_set_outover(pin, GPIO_OVERRIDE_INVERT);
		if (pin != PIN_SCSI_DBP)
		{
			gpio_set_inover(pin, GPIO_OVERRIDE_INVERT);
		}
		gpio_set_oeover(pin, GPIO_OVERRIDE_LOW);	// Not driven until DataDrive(true).
	}
	pio_gpio_init(m_pio, PIN_SCSI_REQ);
	gpio_disable_pulls(PIN_SCSI_REQ);

	scsi_tx_program_init(m_pio, m_smTx, m_offsetTx, PIN_SCSI_DB0, PIN_SCSI_REQ);
	scsi_rx_program_init(m_pio, m_smRx, m_offsetRx, PIN_SCSI_DB0, PIN_SCSI_REQ);
	uint32_t mask = DATA_MASK | (1u << PIN_SCSI_REQ);
	pio_sm_set_pins_with_mask(m_pio, m_smTx, 1u << PIN_SCSI_REQ, mask);		// REQ released, data 0 (released after inversion).
	pio_sm_set_pindirs_with_mask(m_pio, m_smTx, mask, mask);				// The GPIO oeover decides if data is driven.

	m_dmaChan = dma_claim_unused_channel(true);

	ResetStateMachines();
	return true;	// Like the ACSI variant: USB and serial keep working even if no drive is visible.
}

void Scsi::ResetStateMachines(void)
{
	if (m_dmaChan >= 0)
	{
		dma_channel_abort(m_dmaChan);
	}
	const uint sms[] = {m_smTx, m_smRx};
	const uint offsets[] = {m_offsetTx, m_offsetRx};
	for (int i = 0; i < 2; ++i)
	{
		pio_sm_set_enabled(m_pio, sms[i], false);
		pio_sm_clear_fifos(m_pio, sms[i]);
		pio_sm_restart(m_pio, sms[i]);
		pio_sm_exec(m_pio, sms[i], pio_encode_jmp(offsets[i]));
	}
	pio_sm_set_pins_with_mask(m_pio, m_smTx, 1u << PIN_SCSI_REQ, 1u << PIN_SCSI_REQ);	// Release REQ.
	pio_sm_set_enabled(m_pio, m_smTx, true);	// Both wait for a byte count.
	pio_sm_set_enabled(m_pio, m_smRx, true);
}

bool Scsi::IsReset(void) const
{
	return !gpio_get(PIN_SCSI_RST);
}

bool Scsi::IsAtn(void) const
{
	return !gpio_get(PIN_SCSI_ATN);
}

// Switches the data bus buffer direction without ever having both sides driving.
void Scsi::DataDrive(bool drive)
{
	gpio_put(PIN_SCSI_DATA_OE, true);	// Disable buffer.
	if (drive)
	{
		for (uint pin = PIN_SCSI_DB0; pin <= PIN_SCSI_DBP; ++pin)
		{
			gpio_set_oeover(pin, GPIO_OVERRIDE_NORMAL);	// Pio drives the pins.
		}
		gpio_put(PIN_SCSI_DATA_DIR, true);
	}
	else
	{
		gpio_put(PIN_SCSI_DATA_DIR, false);
		for (uint pin = PIN_SCSI_DB0; pin <= PIN_SCSI_DBP; ++pin)
		{
			gpio_set_oeover(pin, GPIO_OVERRIDE_LOW);	// Pins are inputs, DBP is released by its pullup.
		}
	}
	gpio_put(PIN_SCSI_DATA_OE, false);	// Enable buffer.
}

void Scsi::ReleaseBus(void)
{
	DataDrive(false);
	ResetStateMachines();
	Assert(PIN_SCSI_MSG, false);
	Assert(PIN_SCSI_CD, false);
	Assert(PIN_SCSI_IO, false);
	Assert(PIN_SCSI_BSY_OUT, false);
}

// Sets MSG, C/D and I/O. Data is driven by us when I/O is asserted.
void Scsi::SetPhase(Phase phase)
{
	bool io = (phase & 1) != 0;
	if (!io)
	{
		DataDrive(false);
	}
	Assert(PIN_SCSI_MSG, (phase & 4) != 0);
	Assert(PIN_SCSI_CD, (phase & 2) != 0);
	Assert(PIN_SCSI_IO, io);
	busy_wait_at_least_cycles(m_settleCycles);
	if (io)
	{
		DataDrive(true);
		busy_wait_at_least_cycles(m_settleCycles);
	}
}

// Waits until this target is selected. Returns false if the bus is reset.
bool Scsi::WaitForSelection(void)
{
	const uint32_t selMask = (1u << PIN_SCSI_SEL) | (1u << PIN_SCSI_BSY_IN);
	const uint32_t selected = (1u << PIN_SCSI_BSY_IN);	// SEL asserted (low), BSY released (high).
	while (true)
	{
		if (IsReset())
		{
			return false;
		}
		if ((gpio_get_all() & selMask) == selected)
		{
			busy_wait_at_least_cycles(m_settleCycles);
			uint32_t all = gpio_get_all();
			if ((all & selMask) == selected)
			{
				uint8_t ids = (uint8_t)(all >> PIN_SCSI_DB0);	// Inverted pads: asserted is 1.
				uint8_t mine = ids & m_idMask;
				if (mine != 0)
				{
					int32_t id = __builtin_ctz(mine);
					m_device = id - m_baseId;
					Assert(PIN_SCSI_BSY_OUT, true);
					return true;
				}
			}
		}
	}
}

bool Scsi::WaitAckReleased(void)
{
	uint64_t timeEnd = time_us_64() + TIMEOUT_US;
	while (!gpio_get(PIN_SCSI_ACK))
	{
		if (IsReset() || time_us_64() > timeEnd)
		{
			m_aborted = true;
			return false;
		}
	}
	return true;
}

void Scsi::Encode(const uint8_t* src, uint16_t* dst, uint32_t len) const
{
	for (uint32_t i = 0; i < len; ++i)
	{
		dst[i] = m_parity[src[i]];
	}
}

void Scsi::StartSend(const uint16_t* encoded, uint32_t len)
{
	pio_sm_put(m_pio, m_smTx, len - 1);
	dma_channel_config c = dma_channel_get_default_config(m_dmaChan);
	channel_config_set_transfer_data_size(&c, DMA_SIZE_16);	// Replicated to 32 bits, the pio uses the low 9.
	channel_config_set_read_increment(&c, true);
	channel_config_set_write_increment(&c, false);
	channel_config_set_dreq(&c, pio_get_dreq(m_pio, m_smTx, true));
	dma_channel_configure(m_dmaChan, &c, &m_pio->txf[m_smTx], encoded, len, true);
}

void Scsi::StartReceive(uint8_t* data, uint32_t len)
{
	pio_sm_put(m_pio, m_smRx, len - 1);
	dma_channel_config c = dma_channel_get_default_config(m_dmaChan);
	channel_config_set_transfer_data_size(&c, DMA_SIZE_8);	// Byte is in bits 0-7.
	channel_config_set_read_increment(&c, false);
	channel_config_set_write_increment(&c, true);
	channel_config_set_dreq(&c, pio_get_dreq(m_pio, m_smRx, false));
	dma_channel_configure(m_dmaChan, &c, data, &m_pio->rxf[m_smRx], len, true);
}

// Waits for the pio to finish the last handshake of StartSend.
bool Scsi::WaitSendDone(void)
{
	uint64_t timeEnd = time_us_64() + TIMEOUT_US;
	uint32_t remaining = dma_channel_hw_addr(m_dmaChan)->transfer_count;
	while (pio_sm_is_rx_fifo_empty(m_pio, m_smTx))
	{
		uint32_t now = dma_channel_hw_addr(m_dmaChan)->transfer_count;
		if (now != remaining)
		{
			remaining = now;
			timeEnd = time_us_64() + TIMEOUT_US;	// Progress.
		}
		if (IsReset() || time_us_64() > timeEnd)
		{
			m_aborted = true;
			ResetStateMachines();
			return false;
		}
	}
	pio_sm_get(m_pio, m_smTx);
	return true;
}

bool Scsi::WaitReceiveDone(void)
{
	uint64_t timeEnd = time_us_64() + TIMEOUT_US;
	uint32_t remaining = dma_channel_hw_addr(m_dmaChan)->transfer_count;
	while (dma_channel_is_busy(m_dmaChan))
	{
		uint32_t now = dma_channel_hw_addr(m_dmaChan)->transfer_count;
		if (now != remaining)
		{
			remaining = now;
			timeEnd = time_us_64() + TIMEOUT_US;
		}
		if (IsReset() || time_us_64() > timeEnd)
		{
			m_aborted = true;
			ResetStateMachines();
			return false;
		}
	}
	if (!WaitAckReleased())
	{
		ResetStateMachines();
		return false;
	}
	return true;
}

bool Scsi::SendBytes(const uint8_t* data, uint32_t len)
{
	if (len == 0)
	{
		return true;
	}
	Encode(data, m_smallEnc, len);
	StartSend(m_smallEnc, len);
	return WaitSendDone();
}

bool Scsi::ReceiveBytes(uint8_t* data, uint32_t len)
{
	if (len == 0)
	{
		return true;
	}
	StartReceive(data, len);
	return WaitReceiveDone();
}

uint32_t Scsi::LunError(void) const
{
	return m_drives->GetError(m_device) == NEED_UPDATE ? SENSE_MEDIUM_CHANGED : SENSE_NONE;
}

// Sends num sectors starting at lba. The next chunk is read from the SD card while the current one is sent.
// All data is sent even if the SD card fails. Returns false on SD errors; m_aborted tells if the initiator went away.
bool Scsi::SendSectors(uint32_t lba, uint32_t num)
{
	bool ok = true;
	uint32_t endLba = lba + num;
	uint32_t chunk = num < CHUNK_SECTORS ? num : CHUNK_SECTORS;
	int cur = 0;
	ok &= m_drives->Read(m_device, lba, 0, chunk * LOGICAL_BLOCK, m_buf[cur]);
	Encode(m_buf[cur], m_enc[cur], chunk * LOGICAL_BLOCK);
	while (true)
	{
		StartSend(m_enc[cur], chunk * LOGICAL_BLOCK);
		uint32_t nextLba = lba + chunk;
		uint32_t next = endLba - nextLba;
		if (next > CHUNK_SECTORS)
		{
			next = CHUNK_SECTORS;
		}
		if (next != 0)
		{
			ok &= m_drives->Read(m_device, nextLba, 0, next * LOGICAL_BLOCK, m_buf[cur ^ 1]);
			Encode(m_buf[cur ^ 1], m_enc[cur ^ 1], next * LOGICAL_BLOCK);
		}
		if (!WaitSendDone() || next == 0)
		{
			break;
		}
		lba = nextLba;
		chunk = next;
		cur ^= 1;
	}
	return ok;
}

// Receives num sectors and writes them starting at lba. The next chunk is received while the current one is written.
// Only completely received chunks are written. Returns false on SD errors.
bool Scsi::ReceiveSectors(uint32_t lba, uint32_t num)
{
	bool ok = true;
	uint32_t endLba = lba + num;
	uint32_t chunk = num < CHUNK_SECTORS ? num : CHUNK_SECTORS;
	int cur = 0;
	StartReceive(m_buf[cur], chunk * LOGICAL_BLOCK);
	while (WaitReceiveDone())
	{
		uint32_t nextLba = lba + chunk;
		uint32_t next = endLba - nextLba;
		if (next > CHUNK_SECTORS)
		{
			next = CHUNK_SECTORS;
		}
		if (next != 0)
		{
			StartReceive(m_buf[cur ^ 1], next * LOGICAL_BLOCK);
		}
		ok &= m_drives->Write(m_device, lba, 0, chunk * LOGICAL_BLOCK, m_buf[cur]);
		if (next == 0)
		{
			break;
		}
		lba = nextLba;
		chunk = next;
		cur ^= 1;
	}
	return ok;
}

bool Scsi::Status(uint8_t status)
{
	SetPhase(STATUS);
	return SendBytes(&status, 1);
}

bool Scsi::MessageIn(uint8_t msg)
{
	SetPhase(MSG_IN);
	return SendBytes(&msg, 1);
}

// Receives messages for as long as the initiator asserts ATN.
// Returns false if the connection must end (abort, reset or timeout).
bool Scsi::MessageOut(void)
{
	while (IsAtn())
	{
		SetPhase(MSG_OUT);
		do
		{
			uint8_t msg;
			if (!ReceiveBytes(&msg, 1))
			{
				return false;
			}
			if (msg & MSG_IDENTIFY)
			{
				m_lun = msg & 0x07;
			}
			else if (msg == MSG_EXTENDED)
			{
				// Synchronous or wide transfer requests. Rejecting them means asynchronous 8 bit.
				uint8_t len;
				if (!ReceiveBytes(&len, 1) || !ReceiveBytes(m_small, len == 0 ? 256 : len))
				{
					return false;
				}
				m_reject = true;
			}
			else if (msg == MSG_ABORT || msg == MSG_BUS_DEVICE_RESET)
			{
				m_aborted = true;
				return false;
			}
			else if (msg >= 0x20 && msg <= 0x2f)
			{
				// Two byte messages (queue tags).
				uint8_t second;
				if (!ReceiveBytes(&second, 1))
				{
					return false;
				}
				m_reject = true;
			}
			else if (msg != MSG_NOP)
			{
				m_reject = true;
			}
		} while (IsAtn());

		if (m_reject)
		{
			m_reject = false;
			if (!MessageIn(MSG_REJECT))
			{
				return false;
			}
		}
	}
	return true;
}

int Scsi::CheckCondition(uint32_t sense)
{
	if (m_device >= 0 && m_device < 8)
	{
		m_sense[m_device] = sense;
	}
	return STATUS_CHECK_CONDITION;
}

int Scsi::CmdRequestSense(void)
{
	uint32_t len = m_cdb[4];
	if (len == 0)
	{
		len = 4;	// SCSI-1 initiators use 0 for 4 bytes.
	}
	if (len > 18)
	{
		len = 18;
	}
	uint32_t sense = m_lun != 0 ? SENSE_LUN_NOT_SUPPORTED : m_sense[m_device];
	memset(m_small, 0, 18);
	m_small[0] = 0x70;	// Current error, fixed format.
	m_small[2] = (uint8_t)(sense >> 16);
	m_small[7] = 10;
	m_small[12] = (uint8_t)(sense >> 8);
	m_small[13] = (uint8_t)sense;
	m_sense[m_device] = SENSE_NONE;
	SetPhase(DATA_IN);
	return SendBytes(m_small, len) ? STATUS_GOOD : -1;
}

int Scsi::CmdInquiry(void)
{
	if (m_cdb[1] & 0x01)
	{
		return CheckCondition(SENSE_INVALID_FIELD);	// No vital product data.
	}
	uint32_t len = m_cdb[4];
	if (len > 36)
	{
		len = 36;
	}
	memset(m_small, 0, 36);
	if (m_lun != 0)
	{
		m_small[0] = 0x7f;	// No device on this LUN.
	}
	else
	{
		m_small[0] = 0x00;	// Direct access device.
		m_small[1] = m_drives->IsRemovable(m_device) ? 0x80 : 0x00;
	}
	m_small[2] = 0x02;	// SCSI-2
	m_small[3] = 0x02;	// Response data format
	m_small[4] = 36 - 5;
	m_drives->GetVendor8(m_device, (char*)(m_small + 8));
	m_drives->GetProduct16(m_device, (char*)(m_small + 16));
	m_drives->GetVersion4(m_device, (char*)(m_small + 32));
	SetPhase(DATA_IN);
	return SendBytes(m_small, len) ? STATUS_GOOD : -1;
}

int Scsi::CmdModeSense(void)
{
	bool dbd = (m_cdb[1] & 0x08) != 0;
	uint32_t pageControl = m_cdb[2] >> 6;
	uint32_t page = m_cdb[2] & 0x3f;
	uint32_t blocks = m_drives->GetLSectorCount(m_device);
	if (pageControl == 3)
	{
		return CheckCondition(0x053900);	// Saving parameters not supported.
	}

	memset(m_small, 0, sizeof(m_small));
	uint32_t p = 4;
	m_small[2] = m_drives->IsWriteable(m_device) ? 0x00 : 0x80;	// Write protect
	if (!dbd)
	{
		m_small[3] = 8;
		uint32_t b = blocks > 0xffffff ? 0xffffff : blocks;
		m_small[p + 1] = (uint8_t)(b >> 16);
		m_small[p + 2] = (uint8_t)(b >> 8);
		m_small[p + 3] = (uint8_t)b;
		m_small[p + 6] = (uint8_t)(LOGICAL_BLOCK >> 8);
		m_small[p + 7] = (uint8_t)LOGICAL_BLOCK;
		p += 8;
	}

	// Made up geometry: 16 heads, 63 sectors per track.
	const uint32_t heads = 16;
	const uint32_t sectors = 63;
	uint32_t cylinders = blocks / (heads * sectors);
	bool all = page == 0x3f;
	bool found = false;
	if (all || page == 0x01)
	{
		// Read-write error recovery
		m_small[p] = 0x01;
		m_small[p + 1] = 0x0a;
		p += 12;
		found = true;
	}
	if (all || page == 0x03)
	{
		// Format device
		m_small[p] = 0x03;
		m_small[p + 1] = 0x16;
		if (pageControl != 1)
		{
			m_small[p + 10] = (uint8_t)(sectors >> 8);
			m_small[p + 11] = (uint8_t)sectors;
			m_small[p + 12] = (uint8_t)(LOGICAL_BLOCK >> 8);
			m_small[p + 13] = (uint8_t)LOGICAL_BLOCK;
			m_small[p + 20] = 0x40;	// Hard sectored
		}
		p += 24;
		found = true;
	}
	if (all || page == 0x04)
	{
		// Rigid disk geometry
		m_small[p] = 0x04;
		m_small[p + 1] = 0x16;
		if (pageControl != 1)
		{
			m_small[p + 2] = (uint8_t)(cylinders >> 16);
			m_small[p + 3] = (uint8_t)(cylinders >> 8);
			m_small[p + 4] = (uint8_t)cylinders;
			m_small[p + 5] = (uint8_t)heads;
			m_small[p + 20] = 0x1c;	// 7200 rpm
			m_small[p + 21] = 0x20;
		}
		p += 24;
		found = true;
	}
	if (all || page == 0x08)
	{
		// Caching
		m_small[p] = 0x08;
		m_small[p + 1] = 0x0a;
		p += 12;
		found = true;
	}
	if (!found && page != 0x00)
	{
		return CheckCondition(SENSE_INVALID_FIELD);
	}
	m_small[0] = (uint8_t)(p - 1);

	uint32_t len = m_cdb[4];
	if (len > p)
	{
		len = p;
	}
	SetPhase(DATA_IN);
	return SendBytes(m_small, len) ? STATUS_GOOD : -1;
}

int Scsi::CmdModeSelect(void)
{
	// Parameters are accepted and ignored.
	uint32_t len = m_cdb[4];
	SetPhase(DATA_OUT);
	return ReceiveBytes(m_small, len) ? STATUS_GOOD : -1;
}

int Scsi::CmdReadCapacity(void)
{
	uint32_t blocks = m_drives->GetLSectorCount(m_device);
	SetBE32(m_small, blocks - 1);	// Last block
	SetBE32(m_small + 4, LOGICAL_BLOCK);
	SetPhase(DATA_IN);
	return SendBytes(m_small, 8) ? STATUS_GOOD : -1;
}

int Scsi::CmdReadWrite(bool write, uint32_t lba, uint32_t num)
{
	uint32_t size = m_drives->GetLSectorCount(m_device);
	if (lba > size || (size - lba) < num)
	{
		return CheckCondition(SENSE_LBA_OUT_OF_RANGE);
	}
	if (num == 0)
	{
		return STATUS_GOOD;
	}
	if (write && !m_drives->IsWriteable(m_device))
	{
		return CheckCondition(SENSE_WRITE_PROTECTED);
	}
	bool ok;
	if (write)
	{
		SetPhase(DATA_OUT);
		ok = ReceiveSectors(lba, num);
	}
	else
	{
		SetPhase(DATA_IN);
		ok = SendSectors(lba, num);
	}
	if (m_aborted)
	{
		return -1;
	}
	if (!ok)
	{
		uint32_t sense = LunError();
		return CheckCondition(sense != SENSE_NONE ? sense : (write ? SENSE_WRITE_ERROR : SENSE_READ_ERROR));
	}
	return STATUS_GOOD;
}

int Scsi::CmdVerify(void)
{
	if (m_cdb[1] & 0x02)
	{
		return CheckCondition(SENSE_INVALID_FIELD);	// Byte compare not supported.
	}
	uint32_t lba = GetBE32(m_cdb + 2);
	uint32_t num = GetBE16(m_cdb + 7);
	uint32_t size = m_drives->GetLSectorCount(m_device);
	if (lba > size || (size - lba) < num)
	{
		return CheckCondition(SENSE_LBA_OUT_OF_RANGE);
	}
	return STATUS_GOOD;
}

int Scsi::Command(void)
{
	uint8_t op = m_cdb[0];
	Serial::self->WriteToUsb("SCSI ");
	Serial::self->WriteHexToUsb((uint32_t)(m_device + m_baseId), 1);
	Serial::self->WriteToUsb(": ");
	Serial::self->WriteHexToUsb(op, 2);
	Serial::self->WriteToUsb("\r\n");

	if (op == 0x03)
	{
		return CmdRequestSense();
	}
	if (op == 0x12)
	{
		return CmdInquiry();
	}
	m_sense[m_device] = SENSE_NONE;
	if (m_lun != 0)
	{
		return CheckCondition(SENSE_LUN_NOT_SUPPORTED);
	}
	// The PC has written to the image: tell the Atari once, so it reads FAT and directories again.
	if (m_drives->GetError(m_device) == NEED_UPDATE)
	{
		m_drives->ClearError(m_device);
		return CheckCondition(SENSE_MEDIUM_CHANGED);
	}

	switch (op)
	{
	case 0x00:	// Test unit ready
	case 0x01:	// Rezero unit
	case 0x04:	// Format unit
	case 0x0b:	// Seek(6)
	case 0x1b:	// Start stop unit
	case 0x1d:	// Send diagnostic
	case 0x1e:	// Prevent allow medium removal
	case 0x2b:	// Seek(10)
	case 0x35:	// Synchronize cache
		return STATUS_GOOD;
	case 0x08:	// Read(6)
	case 0x0a:	// Write(6)
	{
		uint32_t lba = ((uint32_t)(m_cdb[1] & 0x1f) << 16) | ((uint32_t)m_cdb[2] << 8) | m_cdb[3];
		uint32_t num = m_cdb[4] == 0 ? 256 : m_cdb[4];
		return CmdReadWrite(op == 0x0a, lba, num);
	}
	case 0x15:	// Mode select(6)
		return CmdModeSelect();
	case 0x1a:	// Mode sense(6)
		return CmdModeSense();
	case 0x25:	// Read capacity
		return CmdReadCapacity();
	case 0x28:	// Read(10)
	case 0x2a:	// Write(10)
		return CmdReadWrite(op == 0x2a, GetBE32(m_cdb + 2), GetBE16(m_cdb + 7));
	case 0x2f:	// Verify(10)
		return CmdVerify();
	case 0xa8:	// Read(12)
	case 0xaa:	// Write(12)
		return CmdReadWrite(op == 0xaa, GetBE32(m_cdb + 2), GetBE32(m_cdb + 6));
	default:
		return CheckCondition(SENSE_ILLEGAL_OPCODE);
	}
}

// Everything from selection to bus free.
void Scsi::HandleConnection(void)
{
	m_aborted = false;
	m_reject = false;
	m_lun = 0;
	bool identified = false;

	// Wait for the initiator to release SEL.
	uint64_t timeEnd = time_us_64() + TIMEOUT_US;
	while (!gpio_get(PIN_SCSI_SEL))
	{
		if (IsReset() || time_us_64() > timeEnd)
		{
			return;
		}
	}

	if (IsAtn())
	{
		m_lun = 0xff;
		if (!MessageOut())
		{
			return;
		}
		identified = m_lun != 0xff;
		if (!identified)
		{
			m_lun = 0;
		}
	}

	SetPhase(COMMAND);
	if (!ReceiveBytes(m_cdb, 1))
	{
		return;
	}
	uint32_t len;
	switch (m_cdb[0] >> 5)
	{
	case 1:
	case 2:
		len = 10;
		break;
	case 4:
		len = 16;
		break;
	case 5:
		len = 12;
		break;
	default:
		len = 6;
		break;
	}
	if (!ReceiveBytes(m_cdb + 1, len - 1))
	{
		return;
	}
	if (!identified)
	{
		m_lun = m_cdb[1] >> 5;	// SCSI-1 style LUN.
	}
	if (IsAtn() && !MessageOut())
	{
		return;
	}

	int status = Command();
	if (status < 0 || m_aborted)
	{
		return;
	}
	if (!Status((uint8_t)status))
	{
		return;
	}
	if (IsAtn() && !MessageOut())
	{
		return;
	}
	MessageIn(MSG_COMMAND_COMPLETE);
}

void __not_in_flash_func(Scsi::Start)(void)
{
	while (true)
	{
		ReleaseBus();
		if (IsReset())
		{
			Serial::self->WriteToUsb("SCSI reset!\r\n");
			while (IsReset())
			{
			}
			continue;
		}
		if (WaitForSelection())
		{
			HandleConnection();
		}
	}
}
