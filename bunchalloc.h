#ifndef _SATISALLOC_H_
#define _SATISALLOC_H_ 1

#include<stddef.h>

void* bunchalloc(size_t alignment, size_t headersize, ...);
void bunchfree(void* ptr);

#endif
