#ifndef CONFIG_DEFINED
#define CONFIG_DEFINED

#include <cstdint>
#include <string.h>

enum Exposure
{
	INVISIBLE,
	READABLE,
	WRITEABLE
}; // Exposure

class Config
{
public:
	struct Drive
	{
		char	imagefile[130];	// Zero ended
		bool 	icd;
		Exposure acsiExposure;
		Exposure usbExposure;
		bool 	partToLun;
		char	volume[11];		// Not zero ended
		char	vendor[8];		// Not zero ended
		char	product[16];	// Not zero ended
		char	version[4];		// Not zero ended
	};

	struct Com
	{
		int 	baud;
		int		stop;
		int		parity;
	};
	
	Config(void);
	~Config(void);
	bool Load(const char* path);

	inline int32_t GetBaseId(void) const {return m_baseId;}
	inline int32_t GetDrives(void) const {return m_numDrive;}
	inline const Drive& GetDrive(int drive) const {return m_drives[drive];}
	inline const Com& GetCom(void) const {return m_com;}
	
private:	
	bool ExtractSettings(char* txt, size_t length);

	Exposure ExposeProp(const char* value) const;
	bool PropertyValue(char* txt, size_t length, size_t* p, char** property, char** value);
	void SkipComment(const char* txt, size_t length, size_t* p) const;
	bool SystemProperty(char* txt, size_t length, size_t* p);
	bool DriveProperty(char* txt, size_t length, size_t* p);
	bool ComProperty(char* txt, size_t length, size_t* p);
	int StructName(const char* txt, size_t length, size_t* p);
	void CopyFillSpace(const char* value, char* dst, size_t length);
	
	int32_t m_baseId;
	int32_t m_numDrive;
	Drive	m_drives[8];
	Com	m_com;

}; // class Config;

#endif // CONFIG_DEFINED
