#ifndef SCSI_DEFINED
#define SCSI_DEFINED

#include <cstdint>
#include <hardware/pio.h>

class DriveInterface;

// SCSI-2 target for the Atari TT (and other SCSI initiators).
// Asynchronous transfers only. Disconnect/reselect and synchronous transfers are not supported.
class Scsi
{
public:
	Scsi(void);
	~Scsi(void);

	bool Init(DriveInterface *drives, int32_t baseId);
	void Start(void);	// Never returns. Runs on core 1.

private:
	enum Phase
	{
		DATA_OUT = 0,
		DATA_IN = 1,	// I/O
		COMMAND = 2,	// C/D
		STATUS = 3,		// C/D, I/O
		MSG_OUT = 6,	// MSG, C/D
		MSG_IN = 7		// MSG, C/D, I/O
	};

	// Bus
	void ReleaseBus(void);
	void ResetStateMachines(void);
	bool IsReset(void) const;
	bool IsAtn(void) const;
	void SetPhase(Phase phase);
	void DataDrive(bool drive);
	bool WaitForSelection(void);
	bool WaitAckReleased(void);

	// Transfers. All return false if the transfer was aborted (reset or timeout).
	bool SendBytes(const uint8_t* data, uint32_t len);
	bool ReceiveBytes(uint8_t* data, uint32_t len);
	void StartSend(const uint16_t* encoded, uint32_t len);
	void StartReceive(uint8_t* data, uint32_t len);
	bool WaitSendDone(void);
	bool WaitReceiveDone(void);
	void Encode(const uint8_t* src, uint16_t* dst, uint32_t len) const;
	bool SendSectors(uint32_t lba, uint32_t num);
	bool ReceiveSectors(uint32_t lba, uint32_t num);

	// Phases
	void HandleConnection(void);
	bool MessageOut(void);
	bool MessageIn(uint8_t msg);
	bool Status(uint8_t status);

	// Commands. Return the status byte, or -1 if the connection was aborted.
	int Command(void);
	int CheckCondition(uint32_t sense);
	int CmdRequestSense(void);
	int CmdInquiry(void);
	int CmdModeSense(void);
	int CmdModeSelect(void);
	int CmdReadCapacity(void);
	int CmdReadWrite(bool write, uint32_t lba, uint32_t num);
	int CmdVerify(void);

	uint32_t LunError(void) const;

	DriveInterface *m_drives;
	int32_t m_baseId;
	uint8_t m_idMask;		// One bit for every SCSI id we answer to.
	int32_t m_device;		// Drive index of the current connection.
	uint32_t m_lun;			// LUN of the current connection (only 0 exists).
	bool m_reject;			// Send MESSAGE REJECT when the initiator lets us.
	bool m_aborted;
	uint8_t m_cdb[16];
	uint32_t m_sense[8];	// Sense key << 16 | ASC << 8 | ASCQ, per SCSI id.

	PIO m_pio;
	uint m_smTx;
	uint m_smRx;
	uint m_offsetTx;
	uint m_offsetRx;
	int m_dmaChan;
	uint32_t m_settleCycles;	// Bus settle delay, 400ns.

	uint8_t* m_buf[2];		// Sector data, CHUNK_SECTORS each.
	uint16_t* m_enc[2];		// Sector data with parity, ready for the pio.
	uint8_t m_small[256];	// Command data that isn't sector data.
	uint16_t m_smallEnc[256];
	uint16_t m_parity[256];	// Byte with parity bit in bit 8.
}; // class Scsi

#endif // SCSI_DEFINED
