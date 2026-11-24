#include "drive/usb_diff_log.h"

#ifdef USB_DIFF_LOGGING
#include <ff.h>
#include "drive/image.h"
#include "drive/logical_block.h"

void LogWriteNamedValue(Image *image, const char* name, uint32_t value)
{
	f_printf(&image->logHandle, "%s: %x\n", name, value);
	f_sync(&image->logHandle);
}

void LogWriteBlockDiff(Image *image, uint8_t* oldData, uint8_t* newData, uint32_t len, uint32_t pos)
{
	uint32_t block = pos / LOGICAL_BLOCK;
	uint32_t offset = pos % LOGICAL_BLOCK;
	f_printf(&image->logHandle, "Block %d, offset %d, len %d diff: ", block, offset, len);
	bool allSame = true;
	for (uint32_t i = offset; i < (offset + len); i += 16)
	{
		bool same = true;
		// Only write 16 byte lines that have changes.
		for (uint32_t j = i; j < (i + 16); ++j)
		{
			if (oldData[i] != newData[i]) {same = false; break;}
		}
		if (!same)
		{
			allSame = false;
			f_printf(&image->logHandle, "\n\t%03x: ", i);
			for (uint32_t j = i; j < (i + 16); ++j)
			{
				if (oldData[i] != newData[i])
				{
					f_printf(&image->logHandle, "%02x!=%02x ", (uint32_t)oldData[i], (uint32_t)newData[i]);
				}
				else
				{
					f_printf(&image->logHandle, "%02x==%02x ", (uint32_t)oldData[i], (uint32_t)newData[i]);
				}
			}
			
		}
	}
	if (allSame)
	{
		f_printf(&image->logHandle, "UNCHANGED");
	}
	f_printf(&image->logHandle, "\n");
	f_sync(&image->logHandle);
}

#endif // USB_DIFF_LOGGING
