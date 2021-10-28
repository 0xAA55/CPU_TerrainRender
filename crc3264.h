#ifndef _CRC3264_H_
#define _CRC3264_H_ 1

#include<stdint.h>
#include<stddef.h>

uint32_t crc32(uint32_t seed, const void *data, size_t len);
uint64_t crc64(uint64_t seed, const void *data, size_t len);

#endif 
