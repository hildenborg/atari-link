#ifndef ACSI_INTERFACE_DEFINED
#define ACSI_INTERFACE_DEFINED

#include "drive/drive_interface.h"

// Acsi should just serve the images as is.
class AcsiInterface : public DriveInterface
{
public:
	virtual bool Read(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert) const;
	virtual bool Write(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert);
	virtual bool IsWriteable(uint32_t lun) const;
	virtual bool IsRemovable(uint32_t lun) const;
	virtual bool IsIcd(uint32_t lun) const;
	virtual DriveError GetError(uint32_t lun) const;
	virtual void ClearError(uint32_t lun) const;
protected:
	friend class DriveCollection;
}; // AcsiInterface

#endif // ACSI_INTERFACE_DEFINED
