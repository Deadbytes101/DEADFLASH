#ifndef DEADFLASH_WIN32_STUB_MALLOC_H
#define DEADFLASH_WIN32_STUB_MALLOC_H
#include <stddef.h>
void *_aligned_malloc(size_t size, size_t alignment);
void _aligned_free(void *pointer);
#endif
