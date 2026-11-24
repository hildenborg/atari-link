#ifndef FATFS_LOCK_DEFINED
#define FATFS_LOCK_DEFINED


// FatFs is built with FF_FS_REENTRANT 0, and both cores (ACSI and USB) access it.
// All images live on the same SD card volume, so one mutex must guard every FatFs call.
void LockFatFs(void);
void UnlockFatFs(void);

#endif // FATFS_LOCK_DEFINED
