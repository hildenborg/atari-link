#ifndef IMAGE_DEFINED
#define IMAGE_DEFINED

#include <cstdint>
#include <map>
#include <ff.h>
#include "config/config.h"
#include "drive/drive_error.h"
#include "drive/logical_block.h"
#include "drive/usb_diff_log.h"

class Image
{
public:
	Image(const Config::Drive &d) 
		: drive(d)
		, acsiError(NO_ERROR)
		, usbError(NO_ERROR)
	{

	}
	~Image(void) {}
	
#ifdef USB_DIFF_LOGGING
	mutable FIL logHandle;
	uint8_t tmpSpace[LOGICAL_BLOCK];
#endif

	mutable FIL fileHandle;
	const Config::Drive &drive;
	DriveError acsiError;
	DriveError usbError;

	// sector num, 512 bytes of converted and/or write protected sectors. Write is allowed, but only occurs in memory and not to disk.
	// Only used by usb
	std::map<uint32_t, uint8_t*> cachedSectors;
}; // Image

#endif // IMAGE_DEFINED
