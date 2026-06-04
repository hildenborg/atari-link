#include <bsp/board.h>
#include "tusb.h"
#include "../image.h"
#include "usb.h"

#define DISK_LOGGING

static DriveInterface *s_drives;

// DiskImage init
void SetDiskImages(DriveInterface *drives)
{
	s_drives = drives;
}


void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
	s_drives->GetVendor8(lun, (char*)vendor_id); 
	s_drives->GetProduct16(lun, (char*)product_id);
	s_drives->GetVersion4(lun, (char*)product_rev);
	s_drives->ClearError(lun);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
	if (s_drives->IsEjected(lun)) {return false;}
	if (s_drives->GetError(lun) == NEED_UPDATE)
	{
		#ifdef DISK_LOGGING
		Serial::self->WriteToUsb("SCSI_SENSE_UNIT_ATTENTION\r\n");
		#endif
		tud_msc_set_sense(lun, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0x00);
		s_drives->ClearError(lun);
		return false;
	}

	return true;
}

// Logical block length and count
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
	*block_count = s_drives->GetPSectorCount(lun);
	*block_size = s_drives->GetPSectorSize(lun);
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
	if (!start && load_eject)
	{
		s_drives->Eject(lun);
	}
	return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
	if (s_drives->IsEjected(lun)) {return -1;}
	#ifdef DISK_LOGGING
	Serial::self->WriteToUsb("tud_msc_read10_cb ");
	Serial::self->WriteHexToUsb(lba, 8);
	Serial::self->WriteToUsb(" ");
	Serial::self->WriteHexToUsb(offset, 8);
	Serial::self->WriteToUsb(" ");
	Serial::self->WriteHexToUsb(bufsize, 8);
	Serial::self->WriteToUsb("\r\n");
	#endif

	if (s_drives->Read(lun, lba, offset, bufsize, buffer))
	{
		return bufsize;
	}
	return -1;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
	if (s_drives->IsEjected(lun)) {return false;}
	return s_drives->IsWriteable(lun);
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
	if (s_drives->IsEjected(lun)) {return -1;}
	#ifdef DISK_LOGGING
	Serial::self->WriteToUsb("tud_msc_write10_cb ");
	Serial::self->WriteHexToUsb(lba, 8);
	Serial::self->WriteToUsb(" ");
	Serial::self->WriteHexToUsb(offset, 8);
	Serial::self->WriteToUsb(" ");
	Serial::self->WriteHexToUsb(bufsize, 8);
	Serial::self->WriteToUsb("\r\n");
	#endif

	if (s_drives->Write(lun, lba, offset, bufsize, buffer))
	{
		return bufsize;
	}
	return -1;
}

// Callback invoked when received an SCSI command not in built-in list
int32_t tud_msc_scsi_cb (uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
	if (scsi_cmd[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL)
	{
		return 0;
	}
	tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
	return -1;
}

uint8_t tud_msc_get_maxlun_cb(void)
{
	return (uint8_t)(s_drives->GetNumOfLuns());
}
