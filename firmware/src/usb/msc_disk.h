#ifndef MSC_DISK_DEFINED
#define MSC_DISK_DEFINED

class DriveInterface;

// Sets the drives exposed as USB mass storage. Call before USB is initialized.
void SetDiskImages(DriveInterface *drives);

#endif // MSC_DISK_DEFINED
