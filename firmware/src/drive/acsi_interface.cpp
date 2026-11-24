#include "drive/acsi_interface.h"
#include <ff.h>
#include "drive/fatfs_lock.h"
#include "drive/image.h"
#include "drive/logical_block.h"

bool AcsiInterface::Read(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert) const
{
	bool result = false;
	const Lun& l = m_luns[lun];
	LockFatFs();
	if (l.image->acsiError != NEED_UPDATE)
	{
		unsigned int numread = 0;
		FSIZE_t position = ((FSIZE_t)(sector + l.l_startSector) * LOGICAL_BLOCK) + offset;
		result = f_lseek(&l.image->fileHandle, position) == FR_OK;
		result &= f_read(&l.image->fileHandle, buffert, length, &numread) == FR_OK;
		result &= numread == length;
		l.image->acsiError = result ? NO_ERROR : RW_ERROR;
	}
	UnlockFatFs();
	return result;
}

bool AcsiInterface::Write(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert)
{
	bool result = false;
	Lun& l = m_luns[lun];
	LockFatFs();
	if (l.image->acsiError != NEED_UPDATE)
	{
		if (l.image->drive.acsiExposure == WRITEABLE)
		{
			unsigned int numwrite = 0;
			FSIZE_t position = ((FSIZE_t)(sector + l.l_startSector) * LOGICAL_BLOCK) + offset;
			result = f_lseek(&l.image->fileHandle, position) == FR_OK;
			result &= f_write(&l.image->fileHandle, buffert, length, &numwrite) == FR_OK;
			result &= numwrite == length;
			l.image->usbError = NEED_UPDATE;	// USB needs to be updated.
			l.image->acsiError = result ? NO_ERROR : RW_ERROR;
		}
		else
		{
			l.image->acsiError = WRITE_PROTECTED;
		}
	}
	UnlockFatFs();
	return result;
}

bool AcsiInterface::IsWriteable(uint32_t lun) const
{
	return m_luns[lun].image->drive.acsiExposure == WRITEABLE;
}

bool AcsiInterface::IsRemovable(uint32_t lun) const
{
	// If usb can write to the image, then acsi needs to treat the image as removable.
	// This is so acsi will update fat etc if changed.
	return m_luns[lun].image->drive.usbExposure == WRITEABLE;
}

bool AcsiInterface::IsIcd(uint32_t lun) const
{
	return m_luns[lun].image->drive.icd;
}


DriveError AcsiInterface::GetError(uint32_t lun) const
{
	return m_luns[lun].image->acsiError;
}

void AcsiInterface::ClearError(uint32_t lun) const
{
	m_luns[lun].image->acsiError = NO_ERROR;
}
