#ifndef TEST_MEMORY_H
#define TEST_MEMORY_H
#include <stdint.h>
#include <stddef.h>
void *test_map_memory(uintptr_t address, size_t size);
void test_unmap_memory(void *address, size_t size);
#endif
