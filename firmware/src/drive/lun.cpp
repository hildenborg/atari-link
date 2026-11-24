#include "drive/lun.h"
#include "drive/logical_block.h"

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
