#include "drive/usb_interface.h"
#include <map>
#include <string.h>
#include <ff.h>
#include "drive/fatfs_lock.h"
#include "drive/image.h"
#include "drive/logical_block.h"
#include "drive/usb_diff_log.h"

/*
	Don't forget that the sector size may not be LOGICAL_BLOCK
*/
bool USBInterface::R_W_CachedSectors(bool read_write, uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert) const
{
	bool result = true;
	const Lun& l = m_luns[lun];
	bool allCached = true;
	
	// Convert sectors to byte positions
	FSIZE_t startPosition = ((FSIZE_t)l.l_startSector * LOGICAL_BLOCK) + ((FSIZE_t)sector * l.p_sectorSize) + offset;
	offset = startPosition % LOGICAL_BLOCK;
	uint32_t blockLen = LOGICAL_BLOCK - offset;
	// Now we have values that will work with our cached sectors.

	FSIZE_t position = startPosition;
	while (length != 0 && result)
	{
		if (blockLen > length)
		{
			blockLen = length;
		}
		uint32_t block = position / LOGICAL_BLOCK;
		uint32_t bPosition = position - startPosition;
		
		std::map<uint32_t, uint8_t*>::iterator it = l.image->cachedSectors.find(block);
		if (it != l.image->cachedSectors.end())
		{
			// Cached.
			if (read_write)
			{
				memcpy((uint8_t*)buffert + bPosition, it->second + offset, blockLen);
			}
			else
			{
#ifdef USB_DIFF_LOGGING
				LogWriteBlockDiff(l.image, it->second + offset, (uint8_t*)buffert + bPosition, blockLen, position);
#endif
				memcpy(it->second + offset, (uint8_t*)buffert + bPosition, blockLen);
			}
		}
		else
		{
			// Not cached
			allCached = false;
			unsigned int numrw = 0;
			result &= f_lseek(&l.image->fileHandle, position) == FR_OK;
			if (result)
			{
				if (read_write)
				{
					result &= f_read(&l.image->fileHandle, (uint8_t*)buffert + bPosition, blockLen, &numrw) == FR_OK;
				}
				else
				{
#ifdef USB_DIFF_LOGGING
					f_read(&l.image->fileHandle, l.image->tmpSpace, blockLen, &numrw);
					f_lseek(&l.image->fileHandle, position);
					LogWriteBlockDiff(l.image, l.image->tmpSpace, (uint8_t*)buffert + bPosition, blockLen, position);
#endif
					f_write(&l.image->fileHandle, (uint8_t*)buffert + bPosition, blockLen, &numrw);
				}
				result &= numrw == blockLen;
			}
		}
		
		position += blockLen;
		length -= blockLen;
		offset = 0;
		blockLen = LOGICAL_BLOCK;
	}
	
	if (result && !allCached && !read_write)
	{
		// If we write information that is not in the cache, then ACSI needs to be updated.
		l.image->acsiError = NEED_UPDATE;	// Acsi needs to be updated.
	}
	return result;
}

bool USBInterface::Read(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert) const
{
	bool result = false;
	const Lun& l = m_luns[lun];
	LockFatFs();
	if (l.image->usbError != NEED_UPDATE)
	{
		result = R_W_CachedSectors(true, lun, sector, offset, length, buffert);
		l.image->usbError = result ? NO_ERROR : RW_ERROR;
	}
	UnlockFatFs();
	return result;
}

bool USBInterface::Write(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert)
{
	bool result = false;
	Lun& l = m_luns[lun];
	LockFatFs();
	if (l.image->usbError != NEED_UPDATE)
	{
		if (l.image->drive.usbExposure == WRITEABLE)
		{
			result = R_W_CachedSectors(false, lun, sector, offset, length, buffert);
			l.image->usbError = result ? NO_ERROR : RW_ERROR;
		}
		else
		{
			l.image->usbError = WRITE_PROTECTED;
		}
	}
	UnlockFatFs();
	return result;
}

bool USBInterface::IsWriteable(uint32_t lun) const
{
	return m_luns[lun].image->drive.usbExposure == WRITEABLE;
}

bool USBInterface::IsRemovable(uint32_t lun) const
{
	// If acsi can write to the image, then usb needs to treat the image as removable.
	// This is so scsi will update fat etc if changed.
	return m_luns[lun].image->drive.acsiExposure == WRITEABLE;
}

bool USBInterface::IsIcd(uint32_t lun) const
{
	// USB don't know or care about icd.
	return false;
}

DriveError USBInterface::GetError(uint32_t lun) const
{
	return m_luns[lun].image->usbError;
}

void USBInterface::ClearError(uint32_t lun) const
{
	m_luns[lun].image->usbError = NO_ERROR;
}
