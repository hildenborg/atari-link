#include <ff.h>

// Used by the SCSI variant (RP2350), which has no RTC.
// Same result as the library's rtc.c gives on the RP2040 when the RTC is not running.
DWORD get_fattime(void)
{
	return 0;
}
