#include <stdio.h>
#include <ff.h>
#include "config.h"

static char default_config[] =
"# Com and Drive structs must begin on a new line without and prepending white space.\n"
"# Struct values must begin with with a white space on a new line.\n"
"# Anything between # and a new line will be ignored.\n"
"\n"
"# RS232 settings (com port).\n"
"Com:\n"
"	Baud:		9600\n"
"	Stop:		1\n"
"	Parity:		0\n"
"\n"
"# General system settings.\n"
"System:\n"
"	# Base id for acsi drives: 0 - 7 (7 reserved for atari system). Default 0.\n"
"	# If using other HD's or printers that occupy id's, then adjust this value.\n"
"	AcsiId:		0\n"
"\n"
"# Drive settings.\n"
"# Up to eight drive structures are allowed, but may fail earlier depending on Atari computer (just try and see).\n"
"# One drive structure is one image file.\n"
"# Important about Acsi and USB exposure:\n"
"#	If USB isn't INVISIBLE, and anyone is WRITEABLE, then the Drive must be treated as removeable to make sure that both sides\n"
"#	are up to date with any other side writing to the drive. This makes accesses slower.\n"
"Drive:\n"
"	# Name of hard disk image file (max 128 chars). Default: ST_HD_1.IMG\n"
"	# TOS and DOS images are allowed\n"
"	Image:		ST_HD_1.IMG\n"
"	# Enable use of ICD commands: ON/OFF. Default: OFF.\n"
"	ICD:		ON\n"
"	# Exposure to Atari computer (acsi port): READABLE/WRITEABLE. Default: WRITEABLE\n"
"	Acsi:		WRITEABLE\n"
"	# Exposure to Host computer (usb port): READABLE/WRITEABLE/INVISIBLE. Default: WRITEABLE\n"
"	USB:		WRITEABLE\n"
"	# Expose each partition as a separate hard disk to USB: YES/NO. Deafult: YES\n"
"	PartToLun:	YES\n"
"	# Volume string (max 11 chars). Default: ATARI_RULES\n"
"	Volume:		ATARI_RULES\n"
"	# Vendor string (max 8 chars). Default: IsPotato\n"
"	Vendor:		IsPotato\n"
"	# Product string (max 16 chars). Default: AtariST SD Drive\n"
"	Product:	AtariST SD Drive\n"
"	# Version string (max 4 chars). Default: 1.0b\n"
"	Version:	1.0b\n"
;

namespace
{
	static const char default_image_name[] = "ST_HD_1.img";		// max length 128
	static const char default_volume[] = "ATARI_RULES";			// must be length 11
	static const char default_vendor[] = "IsPotato";			// must be length 8
	static const char default_product[] = "AtariST SD Drive";	// must be length 16
	static const char default_version[] = "1.0b";				// must be length 4
};

Config::Config(void)
	: m_numDrive(0)
	, m_baseId(0)
{
	Drive d;
	d.acsiExposure = WRITEABLE;
	d.usbExposure = WRITEABLE;
	d.partToLun = true;
	d.icd = false;
	memcpy(d.volume, default_volume, 11);
	memcpy(d.vendor, default_vendor, 8);
	memcpy(d.product, default_product, 16);
	memcpy(d.version, default_version, 4);
	
	for (int i = 0; i < 8; ++i)
	{
		m_drives[i] = d;
	}
	
	m_com.baud = 9600;
	m_com.stop = 1;
	m_com.parity = 0;
}

Config::~Config(void)
{
}

bool Config::Load(const char* path)
{
	bool result = false;
	FIL fileHandle;
	memset(&fileHandle, 0, sizeof(FIL));
	if (FR_OK != f_open(&fileHandle, path, FA_READ))
	{
		// No config.txt file, let's create one to help the user.
		memset(&fileHandle, 0, sizeof(FIL));
		if (FR_OK == f_open(&fileHandle, path, FA_CREATE_NEW|FA_WRITE))
		{
			size_t numwritten;
			f_write(&fileHandle, default_config, strlen(default_config), &numwritten);
		}
	}
	f_close(&fileHandle);
	memset(&fileHandle, 0, sizeof(FIL));
	if (FR_OK == f_open(&fileHandle, path, FA_READ))
	{
		size_t size = f_size(&fileHandle);
		char* txt = new char[size + 2];
		if (txt != NULL)
		{
			size_t numread = 0;
			if ((FR_OK == f_read(&fileHandle, txt, size, &numread)) && (numread == size))
			{
				txt[size] = '\r';
				txt[size + 1] = '\n';
				result = ExtractSettings(txt, size);
			}
			delete[] txt;
		}
		
		f_close(&fileHandle);
	}
	return result;
}

void Config::SkipComment(const char* txt, size_t length, size_t* p) const
{
	size_t i = *p;
	// Skip everything until linebreak.
	while (i < length && txt[i] != '\n' && txt[i] != '\r')
	{
		++i;
	}
	// Skip all linebreaks.
	while (i < length && (txt[i] == '\n' || txt[i] == '\r'))
	{
		++i;
	}

	*p = i;
}

bool Config::PropertyValue(char* txt, size_t length, size_t* p, char** property, char** value)
{
	*property = txt;
	// Find end of line
	size_t eol = 0;
	size_t firstColon = 0;
	size_t afterColon = 0;
	bool gotAll = false;
	for (size_t i = 0; i < length && !gotAll; ++i)
	{
		switch(txt[i])
		{
		case '\n':
		case '\r':
		case '#':
			gotAll = true;
			break;
		case '\t':
		case ' ':
			break;
		case ':':
			if (firstColon != 0) {return false;}
			firstColon = i;
			break;
		default:
			if (afterColon == 0 && firstColon !=0)
			{
				afterColon = i;
			}
			eol = i + 1;	// We want the last non white space of the line.
			break;
		}
	}
	// Treat the line as a comment to forward the pointer.
	SkipComment(txt, length, p);
	
	// We can now analyze the captured line and see if it is valid.
	if (firstColon == 0 || afterColon == 0)
	{
		// No colon, no property
		// No aftercolon, no value
		return false;
	}
	txt[firstColon] = 0;	// Now the propery is a zero ended string.
	*value = txt + afterColon;
	txt[eol] = 0;			// Now the value is a zero ended string.
	return true;
}

/*
	All text properties must be padded with spaces if shorter than specified length.
	Longer properties will be cropped.
*/
void Config::CopyFillSpace(const char* value, char* dst, size_t length)
{
	memset(dst, ' ', length);
	size_t l = strlen(value);
	if (l > length) {l = length;}
	memcpy(dst, value, l);
}

Exposure Config::ExposeProp(const char* value) const
{
	if (strcmp(value, "READABLE") == 0)
	{
		return READABLE;
	}
	if (strcmp(value, "WRITEABLE") == 0)
	{
		return WRITEABLE;
	}
	return INVISIBLE;
}

bool Config::DriveProperty(char* txt, size_t length, size_t* p)
{
	char* property;
	char* value;
	bool result = PropertyValue(txt, length, p, &property, &value);
	if (result)
	{
		Drive& drive = m_drives[m_numDrive - 1];
		if (0 == strcmp("Image", property))
		{
			size_t l = strlen(value);
			if (l > 128)
			{
				result = false;
			}
			else
			{
				memcpy(drive.imagefile, value, l + 1);
			}
		}
		else if (0 == strcmp("ICD", property))
		{
			drive.icd = strcmp(value, "ON") == 0;
		}
		else if (0 == strcmp("Acsi", property))
		{
			drive.acsiExposure = ExposeProp(value);
		}
		else if (0 == strcmp("USB", property))
		{
			drive.usbExposure = ExposeProp(value);
		}
		else if (0 == strcmp("PartToLun", property))
		{
			drive.partToLun = strcmp(value, "YES") == 0;
		}
		else if (0 == strcmp("Volume", property))
		{
			CopyFillSpace(value, drive.volume, 11);
		}
		else if (0 == strcmp("Vendor", property))
		{
			CopyFillSpace(value, drive.vendor, 8);
		}
		else if (0 == strcmp("Product", property))
		{
			CopyFillSpace(value, drive.product, 16);
		}
		else if (0 == strcmp("Version", property))
		{
			CopyFillSpace(value, drive.volume, 4);
		}
		else
		{
			result = false;
		}
	}
	return result;
}

bool Config::ComProperty(char* txt, size_t length, size_t* p)
{
	char* property;
	char* value;
	bool result = PropertyValue(txt, length, p, &property, &value);
	if (result)
	{
		if (0 == strcmp("Baud", property))
		{
			result = 1 == sscanf(value, "%d", &m_com.baud);
		}
		else if (0 == strcmp("Stop", property))
		{
			result = 1 == sscanf(value, "%d", &m_com.stop);
		}
		else if (0 == strcmp("Parity", property))
		{
			result = 1 == sscanf(value, "%d", &m_com.parity);
		}
		else
		{
			result = false;
		}
	}
	return result;
}

bool Config::SystemProperty(char* txt, size_t length, size_t* p)
{
	char* property;
	char* value;
	bool result = PropertyValue(txt, length, p, &property, &value);
	if (result)
	{
		if (0 == strcmp("AcsiId", property))
		{
			result = 1 == sscanf(value, "%d", &m_baseId);
		}
		else
		{
			result = false;
		}
	}
	return result;
}

int Config::StructName(const char* txt, size_t length, size_t* p)
{
	int result = 0;
	if (length >= 6 && strncmp(txt, "Drive:", 6) == 0)
	{
		result = 1;
		m_numDrive++;
		if ((m_baseId + m_numDrive) > 8)
		{
			result = 0;
		}
	}
	else if (length >= 4 && strncmp(txt, "Com:", 4) == 0)
	{
		result = 2;
	}
	else if (length >= 7 && strncmp(txt, "System:", 7) == 0)
	{
		result = 3;
	}
	// Treat the line as a comment to forward the pointer.
	SkipComment(txt, length, p);
	return result;
}


bool Config::ExtractSettings(char* txt, size_t length)
{
	// Line comments begin with #
	// Lines starting with "Drive:" or "Com:" are beginning of respectively structs.
	// Lines starting with tab(s) or space(s) are properties for a previously named struct.
	// Property names always ends with ":" followed by tab(s) or space(s) before value and linebreak.
	
	int structType = 0;	// 1 = Drive, 2 = Com
	bool whitespace = false;
	
	while (length > 0)
	{
		size_t p = 0;
		switch (*txt)
		{
		case '#':
			SkipComment(txt, length, &p);
			whitespace = false;
			break;
		case ' ':
		case '\t':
			whitespace = true;
			++p;
			break;
		case '\n':
		case '\r':
			// Linebreak, ignore
			whitespace = false;
			++p;
			break;
		default:
			// Any other character
			if (whitespace)
			{
				bool result = false;
				if (structType == 1) {result = DriveProperty(txt, length, &p);}
				else if (structType == 2) {result = ComProperty(txt, length, &p);}
				else if (structType == 3) {result = SystemProperty(txt, length, &p);}
				else {return false;}
				if (!result) {return false;}
			}
			else
			{
				structType = StructName(txt, length, &p);
				if (structType == 0) {return false;}
			}
			whitespace = false;
			break;
		}
		length -= p;
		txt += p;
	}
	return true;
}

