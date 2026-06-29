#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define MALLOC_CAP_DMA      (1 << 0)
#define MALLOC_CAP_8BIT     (1 << 1)
#define MALLOC_CAP_SPIRAM   (1 << 2)
#define MALLOC_CAP_INTERNAL (1 << 3)
#define MALLOC_CAP_DEFAULT  (1 << 4)

static inline void *heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}

typedef struct {
    size_t total_free_bytes;
    size_t minimum_free_bytes;
    size_t largest_free_block;
    size_t allocated_bytes;
    size_t total_allocated_bytes;
    size_t total_blocks;
    size_t free_blocks;
} multi_heap_info_t;

static inline void heap_caps_get_info(multi_heap_info_t *info, uint32_t caps) {
    (void)caps;
    info->total_free_bytes = 200 * 1024;
    info->minimum_free_bytes = 150 * 1024;
    info->largest_free_block = 100 * 1024;
    info->allocated_bytes = 50 * 1024;
    info->total_allocated_bytes = 50 * 1024;
    info->total_blocks = 100;
    info->free_blocks = 50;
}
