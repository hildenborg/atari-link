#ifndef LUN_DEFINED
#define LUN_DEFINED

#include <cstdint>

class Image;

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

#endif // LUN_DEFINED
