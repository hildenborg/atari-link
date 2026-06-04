#include <string.h>
#include "image.h"


#ifdef USB_DIFF_LOGGING
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

#endif

namespace
{
	uint32_t GetUint32BE(uint8_t* pt)
	{
		uint32_t b3 = pt[0];
		uint32_t b2 = pt[1];
		uint32_t b1 = pt[2];
		uint32_t b0 = pt[3];
		return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
	}

	uint32_t GetUint32LE(uint8_t* pt)
	{
		uint32_t b3 = pt[3];
		uint32_t b2 = pt[2];
		uint32_t b1 = pt[1];
		uint32_t b0 = pt[0];
		return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
	}

	uint32_t GetUint16LE(uint8_t* pt)
	{
		uint32_t b1 = pt[1];
		uint32_t b0 = pt[0];
		return (b1 << 8) | b0;
	}

	void SetUint32LE(uint8_t* pt, uint32_t value)
	{
		pt[0] = (uint8_t)(value & 0xff);
		pt[1] = (uint8_t)((value >> 8) & 0xff);
		pt[2] = (uint8_t)((value >> 16) & 0xff);
		pt[3] = (uint8_t)((value >> 24) & 0xff);
	}
	
};

Lun::Lun(void)
	: image(0)
	, l_startSector(0)
	, l_sectorCount(0)
	, p_sectorCount(0)
	, p_sectorSize(LOGICAL_BLOCK)
	, loaded(true)
{
}

Lun::Lun(Image* i, uint32_t startSector, uint32_t sectorCount)
	: image(i)
	, l_startSector(startSector)
	, l_sectorCount(sectorCount)
	, p_sectorCount(sectorCount)
	, p_sectorSize(LOGICAL_BLOCK)
	, loaded(true)
{
}

Lun::Lun(const Lun& l)
	: image(l.image)
	, l_startSector(l.l_startSector)
	, l_sectorCount(l.l_sectorCount)
	, p_sectorCount(l.p_sectorCount)
	, p_sectorSize(l.p_sectorSize)
	, loaded(l.loaded)
{
}


DriveInterface::DriveInterface(void)
{
}

DriveInterface::~DriveInterface(void)
{
}

void DriveInterface::GetVolume11(uint32_t lun, char* volume) const
{
	memcpy(volume, m_luns[lun].image->drive.volume, 11);
}

void DriveInterface::GetVendor8(uint32_t lun, char* vendor) const
{
	memcpy(vendor, m_luns[lun].image->drive.vendor, 8);
}

void DriveInterface::GetProduct16(uint32_t lun, char* product) const
{
	memcpy(product, m_luns[lun].image->drive.product, 16);
}

void DriveInterface::GetVersion4(uint32_t lun, char* version) const
{
	memcpy(version, m_luns[lun].image->drive.version, 4);
}

uint32_t DriveInterface::GetNumOfLuns(void) const
{
	return m_luns.size();
}

uint32_t DriveInterface::GetLSectorCount(uint32_t lun) const
{
	return m_luns[lun].l_sectorCount;
}

uint32_t DriveInterface::GetPSectorCount(uint32_t lun) const
{
	return m_luns[lun].p_sectorCount;
}

uint32_t DriveInterface::GetPSectorSize(uint32_t lun) const
{
	return m_luns[lun].p_sectorSize;
}

void DriveInterface::Eject(uint32_t lun)
{
	m_luns[lun].loaded = false;
}

bool DriveInterface::IsEjected(uint32_t lun) const
{
	return !m_luns[lun].loaded;
}


bool AcsiInterface::Read(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert) const
{
	bool result = false;
	const Lun& l = m_luns[lun];
	mutex_enter_blocking(&l.image->mutex);
	if (l.image->acsiError != NEED_UPDATE)
	{
		unsigned int numread = 0;
		uint32_t position = ((sector + l.l_startSector) * LOGICAL_BLOCK) + offset;
		result = f_lseek(&l.image->fileHandle, position) == FR_OK;
		result &= f_read(&l.image->fileHandle, buffert, length, &numread) == FR_OK;
		result &= numread == length;
		l.image->acsiError = result ? RW_ERROR : NO_ERROR;
	}
	mutex_exit(&l.image->mutex);
	return result;
}

bool AcsiInterface::Write(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert)
{
	bool result = false;
	Lun& l = m_luns[lun];
	mutex_enter_blocking(&l.image->mutex);
	if (l.image->acsiError != NEED_UPDATE)
	{
		if (l.image->drive.acsiExposure == WRITEABLE)
		{
			unsigned int numwrite = 0;
			uint32_t position = ((sector + l.l_startSector) * LOGICAL_BLOCK) + offset;
			result = f_lseek(&l.image->fileHandle, position) == FR_OK;
			result &= f_write(&l.image->fileHandle, buffert, length, &numwrite) == FR_OK;
			result &= numwrite == length;
			l.image->usbError = NEED_UPDATE;	// USB needs to be updated.
			l.image->acsiError = result ? RW_ERROR : NO_ERROR;
		}
		else
		{
			l.image->acsiError = WRITE_PROTECTED;
		}
	}
	mutex_exit(&l.image->mutex);
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


/*
	Don't forget that the sector size may not be LOGICAL_BLOCK
*/
bool USBInterface::R_W_CachedSectors(bool read_write, uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert) const
{
	bool result = true;
	const Lun& l = m_luns[lun];
	bool allCached = true;
	
	// Convert sectors to byte positions
	uint32_t startPosition = (l.l_startSector * LOGICAL_BLOCK) + (sector * l.p_sectorSize) + offset;
	offset = startPosition % LOGICAL_BLOCK;
	uint32_t blockLen = LOGICAL_BLOCK - offset;
	// Now we have values that will work with our cached sectors.

	uint32_t position = startPosition;
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
	mutex_enter_blocking(&l.image->mutex);
	if (l.image->usbError != NEED_UPDATE)
	{
		result = R_W_CachedSectors(true, lun, sector, offset, length, buffert);
		l.image->usbError = result ? RW_ERROR : NO_ERROR;
	}
	mutex_exit(&l.image->mutex);
	return result;
}

bool USBInterface::Write(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert)
{
	bool result = false;
	Lun& l = m_luns[lun];
	mutex_enter_blocking(&l.image->mutex);
	if (l.image->usbError != NEED_UPDATE)
	{
		if (l.image->drive.usbExposure == WRITEABLE)
		{
			result = R_W_CachedSectors(false, lun, sector, offset, length, buffert);
			l.image->usbError = result ? RW_ERROR : NO_ERROR;
		}
		else
		{
			l.image->usbError = WRITE_PROTECTED;
		}
	}
	mutex_exit(&l.image->mutex);
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



DriveCollection::DriveCollection(void)
{
	m_acsiInterface = new AcsiInterface();
	m_usbInterface = new USBInterface();
}

DriveCollection::~DriveCollection(void)
{
}


bool DriveCollection::LoadImage(const Config::Drive &drive)
{
	bool result = false;
	Image* image = new Image(drive);
#ifdef USB_DIFF_LOGGING
	memset(&image->logHandle, 0, sizeof(FIL));
#endif
	memset(&image->fileHandle, 0, sizeof(FIL));
	mutex_init(&image->mutex);
	
	if (FR_OK == f_open(&image->fileHandle, image->drive.imagefile, FA_READ | FA_WRITE))
	{
#ifdef USB_DIFF_LOGGING
		char tmp[256];
		sprintf(tmp, "%s.log", image->drive.imagefile);
		f_open(&image->logHandle, tmp, FA_CREATE_ALWAYS | FA_WRITE);
#endif
		m_images.push_back(image);
		// Get disk type
		unsigned int numread = 0;
		char diskType[2];
		f_lseek(&image->fileHandle, 0x1fe);
		f_read(&image->fileHandle, diskType, 2, &numread);
		if (diskType[0] == 0x55 && diskType[1] == 0xaa)
		{
			// DOS format
			InitDOSImage(image);
		}
		else
		{
			// TOS format
			InitTOSImage(image);
		}
		result = true;
	}
	else
	{
		delete image;
	}
	return result;
}


DriveInterface* DriveCollection::GetAcsiInterface(void)
{
	return m_acsiInterface;
}

DriveInterface* DriveCollection::GetUSBInterface(void)
{
	return m_usbInterface;
}

void DriveCollection::InitDOSImage(Image* image)
{
	// DOS images are exposed as is to USB, but we write protect (cache in memory) mbr, vbr and ebr sectors.
	// The image is exposed to acsi and usb in the same way, so image and mbr gets a lun
	uint32_t diskSize = f_size(&image->fileHandle) / LOGICAL_BLOCK;
	Lun lun(image, 0, diskSize);
	m_acsiInterface->m_luns.push_back(lun);

	if (image->drive.usbExposure == INVISIBLE)
	{
		// Not exposed to usb
		return;
	}
	if (image->drive.partToLun == false)
	{
		// We will expose the drive as a single lun to usb and let the host deal with the partitions.
		m_usbInterface->m_luns.push_back(lun);
	}
	
	uint8_t *pt_mbr = GetLogicalBlock(image, 0);
	image->cachedSectors[0] = pt_mbr;
#ifdef USB_DIFF_LOGGING
	LogWriteNamedValue(image, "Cached DOS sector", 0);
#endif
	uint8_t *pt_ebr = NULL;
	uint32_t firstEbr = 0;
	uint32_t lastEbr = 0;
	uint32_t maxP = 4;
	do
	{
		for (uint32_t p = 0; p < maxP; ++p)
		{
			uint8_t *pt_p = pt_mbr + 0x1be + (p * 16);
			uint8_t type = pt_p[4];
			uint32_t l_startSector = GetUint32LE(pt_p + 8); 
			uint32_t l_sectorCount = GetUint32LE(pt_p + 12); 
			if (type == 0x5 || type == 0xf)
			{
				// extended boot partition
				l_startSector += firstEbr;	// firstEbr is 0 until we find the first extended partition.
				lastEbr = l_startSector;
				if (firstEbr == 0)
				{
					firstEbr = l_startSector;
				}
				pt_ebr = GetLogicalBlock(image, l_startSector);
				image->cachedSectors[l_startSector] = pt_ebr;
#ifdef USB_DIFF_LOGGING
				LogWriteNamedValue(image, "Cached DOS sector", l_startSector);
#endif
				break;	// Extended boot partition is always the last in list.
			}		
			else if (type == 0x4 || type == 0x6)
			{
				l_startSector += lastEbr;	// lastEbr is 0 until we actually are inside an extended partition.
				uint8_t *pt_vbr = GetLogicalBlock(image, l_startSector);
				image->cachedSectors[l_startSector] = pt_vbr;
#ifdef USB_DIFF_LOGGING
				LogWriteNamedValue(image, "Cached DOS sector", l_startSector);
#endif
				if (image->drive.partToLun == true)
				{
					// USB needs another lun
					Lun usbLun(image, l_startSector, l_sectorCount);
					usbLun.p_sectorSize = GetUint16LE(pt_vbr + 0x00b);
					usbLun.p_sectorCount = (l_sectorCount * LOGICAL_BLOCK) / usbLun.p_sectorSize;
					m_usbInterface->m_luns.push_back(usbLun);
				}
			}
		}
		// Set up next loop to handle ebr if existing.
		maxP = 2;
		pt_mbr = pt_ebr;
		pt_ebr = NULL;
	} while (pt_mbr != NULL);
}

void DriveCollection::InitTOSImage(Image* image)
{
	// TOS images are exposed to USB in a interperated form.
	// This is done by altering mbr, vbr and ebr sectors to look like DOS sectors.
	// They are only in memory, so no alterations are made to TOS disk.
	// TOS partitions are exposed to USB as separate LUN's, as windows do not understand USB drives with 1024 byte physical sectors
	// unless they are separated into separate drives. (looks like a windows bug to me)
	// acsi gets one lun, and usb gest one for each partition.
	uint32_t diskSize = f_size(&image->fileHandle) / LOGICAL_BLOCK;
	Lun lun(image, 0, diskSize);
	m_acsiInterface->m_luns.push_back(lun);

	if (image->drive.usbExposure == INVISIBLE)
	{
		// Not exposed to usb
		return;
	}
	if (image->drive.partToLun == false)
	{
		// We will expose the drive as a single lun to usb and let the host deal with the partitions.
		m_usbInterface->m_luns.push_back(lun);
	}
	
	uint8_t *pt_mbr = GetLogicalBlock(image, 0);
	image->cachedSectors[0] = pt_mbr;
#ifdef USB_DIFF_LOGGING
	LogWriteNamedValue(image, "Cached TOS sector", 0);
#endif
	uint8_t *pt_ebr = NULL;
	uint32_t firstEbr = 0;
	uint32_t lastEbr = 0;
	uint32_t maxP = 4;
	uint8_t partitionType[4];
	uint32_t partitionSectors[8];
	do
	{
		for (uint32_t p = 0; p < maxP; ++p)
		{
			uint8_t *pt_p = pt_mbr + 0x1c6 + (p * 12);
			uint8_t type = 0;
			uint32_t l_startSector = GetUint32BE(pt_p + 4); 
			uint32_t l_sectorCount = GetUint32BE(pt_p + 8); 
			partitionSectors[(p * 2)] = l_startSector;
			partitionSectors[(p * 2) + 1] = l_sectorCount;
			if ((pt_p[0] & 0x81) != 0)
			{
				if (pt_p[1] == 'X' && pt_p[2] == 'G' && pt_p[3] == 'M')
				{
					// XGM
					type = 0xf;	// extended boot partition with LBA
					l_startSector += firstEbr;	// firstEbr is 0 until we find the first extended partition.
					lastEbr = l_startSector;
					if (firstEbr == 0)
					{
						firstEbr = l_startSector;
					}
					pt_ebr = GetLogicalBlock(image, l_startSector);
					image->cachedSectors[l_startSector] = pt_ebr;
#ifdef USB_DIFF_LOGGING
					LogWriteNamedValue(image, "Cached TOS sector", l_startSector);
#endif
					break;	// Extended boot partition is always the last in list.
				}		
				else
				{
					// GEM or BGM
					type = 0x4;	// FAT16
					l_startSector += lastEbr;	// lastEbr is 0 until we actually are inside an extended partition.
					uint8_t *pt_vbr = GetLogicalBlock(image, l_startSector);
					image->cachedSectors[l_startSector] = pt_vbr;
#ifdef USB_DIFF_LOGGING
					LogWriteNamedValue(image, "Cached TOS sector", l_startSector);
#endif
					// Modify to DOS format
					ModifyVBR(image, pt_vbr);
					if (image->drive.partToLun == true)
					{
						// USB needs another lun
						Lun usbLun(image, l_startSector, l_sectorCount);
						usbLun.p_sectorSize = GetUint16LE(pt_vbr + 0x00b);
						usbLun.p_sectorCount = (l_sectorCount * LOGICAL_BLOCK) / usbLun.p_sectorSize;
						m_usbInterface->m_luns.push_back(usbLun);
					}
				}
			}
			partitionType[p] = type;
		}
		if (image->drive.partToLun == false)
		{
			// Need to modify MBR and EBR
			ModifyMBR(pt_mbr, partitionType, partitionSectors, maxP);
		}
		
		// Set up next loop to handle ebr if existing.
		maxP = 2;
		//delete pt_mbr;	// As we do not use any partition information, we can just ignore the data.
		pt_mbr = pt_ebr;
		pt_ebr = NULL;
	} while (pt_mbr != NULL);
}

// Only modifies in memory, not on disk.
void DriveCollection::ModifyVBR(Image* image, uint8_t* pt_vbr)
{
	// Volume label
	memcpy((char*)pt_vbr + 0x02b, image->drive.volume, 11);
	
	// Modify TOS VBR to mimic a DOS VBR
	const uint8_t firstDosBytes[] = {0xEB, 0x3C, 0x90, 0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30};
	memcpy(pt_vbr, firstDosBytes, 0x0b);
	memset(pt_vbr + 0x1e, 0, 0x200 - 0x1e);

	pt_vbr[0x024] = 0x80;	// Fixed disk
	pt_vbr[0x026] = 0x29;	// Extended boot signature

	// Volume ID "B00B50xx"
	pt_vbr[0x027] = (uint8_t)(m_usbInterface->m_luns.size());
	pt_vbr[0x028] = 0x50;
	pt_vbr[0x029] = 0x0b;
	pt_vbr[0x02a] = 0xb0;

	// 4.0 format identifier
	const uint8_t fileSystemType16[] = "FAT16   ";
	memcpy(pt_vbr + 0x036, fileSystemType16, 8);

	// Magic number
	pt_vbr[0x1fe] = 0x55;
	pt_vbr[0x1ff] = 0xaa;
}

// Only modifies in memory, not on disk.
void DriveCollection::ModifyMBR(uint8_t* pt_mbr, uint8_t partitionType[4], uint32_t partitionSectors[8], uint32_t maxP)
{
	memset(pt_mbr, 0, 0x200);
	// Yes we are an MBR/EBR.
	pt_mbr[0x0] = 0xfa;
	pt_mbr[0x1] = 0x33;
	// Partition information.
	for (int32_t p = 0; p < maxP; ++p)
	{
		uint8_t *pt_a = pt_mbr + 0x1be + (p * 16);
		pt_a[0] = 0x80; // bootable. Some detect it as active.
		pt_a[4] = partitionType[p];
		SetUint32LE(pt_a + 8, partitionSectors[(p * 2)]);
		SetUint32LE(pt_a + 12, partitionSectors[(p * 2)] + 1);
	}
	// Magic number
	pt_mbr[0x1fe] = 0x55;
	pt_mbr[0x1ff] = 0xaa;
}

uint8_t* DriveCollection::GetLogicalBlock(Image* image, uint32_t block)
{
	unsigned int numread = 0;
	uint8_t *pt_block = new uint8_t[LOGICAL_BLOCK];
	f_lseek(&image->fileHandle, block * LOGICAL_BLOCK);
	f_read(&image->fileHandle, pt_block, LOGICAL_BLOCK, &numread);
	return pt_block;
}
