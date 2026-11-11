#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define MALLOC_CAP_DMA 8
void *heap_caps_malloc(size_t n, unsigned caps);
void *heap_caps_malloc_prefer(size_t n, size_t count, ...);
void heap_caps_free(void *p);
