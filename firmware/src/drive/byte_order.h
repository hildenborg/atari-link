#ifndef BYTE_ORDER_DEFINED
#define BYTE_ORDER_DEFINED

#include <cstdint>

// Unaligned little and big endian access.

inline uint32_t GetUint32BE(uint8_t* pt)
{
	uint32_t b3 = pt[0];
	uint32_t b2 = pt[1];
	uint32_t b1 = pt[2];
	uint32_t b0 = pt[3];
	return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
}

inline uint32_t GetUint32LE(uint8_t* pt)
{
	uint32_t b3 = pt[3];
	uint32_t b2 = pt[2];
	uint32_t b1 = pt[1];
	uint32_t b0 = pt[0];
	return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
}

inline uint32_t GetUint16LE(uint8_t* pt)
{
	uint32_t b1 = pt[1];
	uint32_t b0 = pt[0];
	return (b1 << 8) | b0;
}

inline void SetUint32LE(uint8_t* pt, uint32_t value)
{
	pt[0] = (uint8_t)(value & 0xff);
	pt[1] = (uint8_t)((value >> 8) & 0xff);
	pt[2] = (uint8_t)((value >> 16) & 0xff);
	pt[3] = (uint8_t)((value >> 24) & 0xff);
}

#endif // BYTE_ORDER_DEFINED
