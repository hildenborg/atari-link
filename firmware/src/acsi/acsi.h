#ifndef ACSI_DEFINED
#define ACSI_DEFINED

#include <hardware/pio.h>

class DriveInterface;

class Acsi
{
public:
	Acsi(void);
	~Acsi(void);

	bool Init(DriveInterface *drives, int32_t baseId);
	void Start(void);
	void Stop(void);
	
private:
	bool GetWithTimeout(uint32_t timeout, uint32_t* data) const;
	bool PioPut(uint32_t data);
	uint32_t PioGet(void);
	int SeekAddressGetNum(uint32_t *seekAddress) const;
	int SeekAddressGetNum10(uint32_t *seekAddress) const;

	void StartDma(bool toPio, uint32_t* buf, uint32_t words);
	bool WaitDma(void);
	bool SendSectorsToAtari(uint32_t pos, uint32_t num);
	bool ReceiveSectorsFromAtari(uint32_t pos, uint32_t num);
	uint32_t ReadWriteError(void) const;

	uint32_t Command_03(void);
	uint32_t Command_08(void);
	uint32_t Command_0A(void);
	uint32_t Command_12(void);
	uint32_t Command_1f(void);
	uint32_t Command_25(void);
	uint32_t Command_28(void);
	uint32_t Command_2a(void);
	uint32_t HardDiskCommand(void);

	void ResetPio(void);
	void ClearAllErrors(void);
	void PrintPioInfo(void);
	void PrintNameNumLn(const char* name, uint32_t num, int len);

private:
	DriveInterface *m_drives;
	int32_t m_baseId;
	uint m_pio_offset;	
	uint32_t m_cmd;
	int32_t m_device;
	uint32_t m_errorCode;
	volatile bool m_reset;	// Set when the Atari resets in the middle of a command.
	uint32_t* m_buf[2];	// Double buffers for SD <-> Atari transfers, CHUNK_SECTORS each.
	uint8_t* m_dataBuf;	// Alias of m_buf[0], used by the small commands.
	int m_dmaChan;
	uint32_t m_cmdBuf[4];
	uint8_t m_fastResponse[256];

}; // class Acsi

#endif // ACSI_DEFINED
