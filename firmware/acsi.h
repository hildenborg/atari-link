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
	int SeekAddressGetNum(uint32_t *seekAddress) const;
	int SeekAddressGetNum10(uint32_t *seekAddress) const;

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
	
	uint32_t m_cmd;
	int32_t m_device;
	uint32_t m_errorCode;
	uint8_t* m_dataBuf;
	uint32_t m_cmdBuf[4];
	uint8_t m_fastResponse[256];

}; // class Acsi

#endif // ACSI_DEFINED
