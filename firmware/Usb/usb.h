#ifndef USB_DEFINED
#define USB_DEFINED

#include <pico/sync.h>

class Config;
class DriveInterface;

void InitUSB(DriveInterface *drives);
void USB_main(void);

#define SERIAL_BUFFER_LEN 1024

class Serial
{
public:
	static Serial *self;

	Serial(Config* config);
	~Serial(void);
	
	#ifdef SERIAL_LOGGING
	void WriteToUsb(const char* src);
	void WriteHexToUsb(uint32_t val, int len);
	#else
	void WriteToUsb(const char* src) {}
	void WriteHexToUsb(uint32_t val, int len) {}
	#endif
	void SendAndReceive(void);
private:
	char m_serialToUsb[SERIAL_BUFFER_LEN];
	int m_s2u_len;
	int m_u2s_len;
    mutex_t m_mutex;
	
}; // Serial

#endif // USB_DEFINED
