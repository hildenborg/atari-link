#include "drive/drive_interface.h"
#include <string.h>
#include "drive/image.h"

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
