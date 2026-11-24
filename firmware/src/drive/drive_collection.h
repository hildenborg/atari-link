#ifndef DRIVE_COLLECTION_DEFINED
#define DRIVE_COLLECTION_DEFINED

#include <cstdint>
#include <vector>
#include "config/config.h"

class Image;
class DriveInterface;
class AcsiInterface;
class USBInterface;

// All hard disk images connected to acsi or usb
class DriveCollection
{
public:
	DriveCollection(void);
	~DriveCollection(void);
	
	bool LoadImage(const Config::Drive &drive);

	DriveInterface* GetAcsiInterface(void);
	DriveInterface* GetUSBInterface(void);
private:
	void InitDOSImage(Image* image);
	void InitTOSImage(Image* image);
	void ModifyVBR(Image* image, uint8_t* pt_vbr);
	void ModifyMBR(uint8_t* pt_mbr, uint8_t partitionType[4], uint32_t partitionSectors[8], uint32_t maxP);
	
	uint8_t* GetLogicalBlock(Image* image, uint32_t block);

	std::vector<Image*> m_images;
	AcsiInterface*	m_acsiInterface;
	USBInterface*	m_usbInterface;
}; // DriveCollection

#endif // DRIVE_COLLECTION_DEFINED
