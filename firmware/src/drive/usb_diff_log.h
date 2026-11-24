#ifndef USB_DIFF_LOG_DEFINED
#define USB_DIFF_LOG_DEFINED

#include <cstdint>

// Stores readable diffs of USB writes to disk.
// Used for debugging USB host destructive writing.
//#define USB_DIFF_LOGGING

#ifdef USB_DIFF_LOGGING
class Image;

void LogWriteNamedValue(Image *image, const char* name, uint32_t value);
void LogWriteBlockDiff(Image *image, uint8_t* oldData, uint8_t* newData, uint32_t len, uint32_t pos);
#endif

#endif // USB_DIFF_LOG_DEFINED
