#include "drive/drive_collection.h"
#include <stdio.h>
#include <string.h>
#include <ff.h>
#include "drive/acsi_interface.h"
#include "drive/byte_order.h"
#include "drive/image.h"
#include "drive/logical_block.h"
#include "drive/lun.h"
#include "drive/usb_diff_log.h"
#include "drive/usb_interface.h"

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
		memset(partitionType, 0, sizeof(partitionType));
		memset(partitionSectors, 0, sizeof(partitionSectors));
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
					partitionType[p] = type;
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
		SetUint32LE(pt_a + 12, partitionSectors[(p * 2) + 1]);
	}
	// Magic number
	pt_mbr[0x1fe] = 0x55;
	pt_mbr[0x1ff] = 0xaa;
}

uint8_t* DriveCollection::GetLogicalBlock(Image* image, uint32_t block)
{
	unsigned int numread = 0;
	uint8_t *pt_block = new uint8_t[LOGICAL_BLOCK];
	f_lseek(&image->fileHandle, (FSIZE_t)block * LOGICAL_BLOCK);
	f_read(&image->fileHandle, pt_block, LOGICAL_BLOCK, &numread);
	return pt_block;
}
