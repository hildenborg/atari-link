#ifndef DRIVE_INTERFACE_DEFINED
#define DRIVE_INTERFACE_DEFINED

#include <cstdint>
#include <vector>
#include "drive/drive_error.h"
#include "drive/lun.h"

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

#endif // DRIVE_INTERFACE_DEFINED
