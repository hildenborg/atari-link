#include "drive/fatfs_lock.h"
#include <pico/sync.h>

auto_init_mutex(s_fatfsMutex);

void LockFatFs(void)
{
	mutex_enter_blocking(&s_fatfsMutex);
}

void UnlockFatFs(void)
{
	mutex_exit(&s_fatfsMutex);
}
