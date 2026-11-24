#ifndef USB_INTERFACE_DEFINED
#define USB_INTERFACE_DEFINED

#include "drive/drive_interface.h"

// USB needs interperation of tos drives and handling of odd physical sector sizes.
// Invisible images do not appear at all here.
class USBInterface : public DriveInterface
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
private:
	bool R_W_CachedSectors(bool read_write, uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert) const;
}; // USBInterface

#endif // USB_INTERFACE_DEFINED
