#ifndef IMAGE_DEFINED
#define IMAGE_DEFINED

#include <vector>
#include <map>
#include <ff.h>
#include <diskio.h>
#include <pico/sync.h>
#include "config.h"

#define LOGICAL_BLOCK 512

// Stores readable diffs of USB writes to disk.
// Used for debugging USB host destructive writing.
//#define USB_DIFF_LOGGING


enum DriveError
{
	NO_ERROR,
	NEED_UPDATE,
	WRITE_PROTECTED,
	RW_ERROR
}; // DriveError

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
    mutable mutex_t mutex;
	const Config::Drive &drive;
	DriveError acsiError;
	DriveError usbError;

	// sector num, 512 bytes of converted and/or write protected sectors. Write is allowed, but only occurs in memory and not to disk.
	// Only used by usb
	std::map<uint32_t, uint8_t*> cachedSectors;
}; // Image

class Lun
{
public:
	Lun(void);
	Lun(Image* i, uint32_t startSector, uint32_t sectorCount);
	Lun(const Lun& l);
	
	Image* image;
	uint32_t l_startSector;
	uint32_t l_sectorCount;
	uint32_t p_sectorCount;
	uint16_t p_sectorSize;
	
	bool loaded;
}; // Lun

class DriveCollection;

class DriveInterface
{
public:
	DriveInterface(void);
	virtual ~DriveInterface(void);
	
	uint32_t GetNumOfLuns(void) const;
//	const Config::Drive& GetConfig(uint32_t lun) const {return m_luns[lun].image->drive;}
	
	virtual bool Read(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert) const = 0;
	virtual bool Write(uint32_t lun, uint32_t sector, uint32_t offset, uint32_t length, void* buffert) = 0;
	virtual bool IsWriteable(uint32_t lun) const = 0;
	virtual bool IsRemovable(uint32_t lun) const = 0;
	virtual bool IsIcd(uint32_t lun) const = 0;
	virtual DriveError GetError(uint32_t lun) const = 0;
	virtual void ClearError(uint32_t lun) const = 0;
	
	void GetVolume11(uint32_t lun, char* volume) const;
	void GetVendor8(uint32_t lun, char* vendor) const;
	void GetProduct16(uint32_t lun, char* product) const;
	void GetVersion4(uint32_t lun, char* version) const;

	uint32_t GetLSectorCount(uint32_t lun) const;
	uint32_t GetPSectorCount(uint32_t lun) const;
	uint32_t GetPSectorSize(uint32_t lun) const;
	
	// Only USB will care about this, so maybe it should be refactored.
	void Eject(uint32_t lun);
	bool IsEjected(uint32_t lun) const;
	
protected:
	friend class DriveCollection;

	std::vector<Lun> m_luns;
}; // DriveInterface

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

#endif // IMAGE_DEFINED
