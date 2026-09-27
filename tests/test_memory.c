#include "test_memory.h"
#include <assert.h>
#ifdef _WIN32
#include <windows.h>
void *test_map_memory(uintptr_t address, size_t size)
{
  void *p = VirtualAlloc((void *)address, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  assert(p == (void *)address);
  return p;
}
void test_unmap_memory(void *address, size_t size)
{
  (void)size;
  assert(VirtualFree(address, 0, MEM_RELEASE));
}
#else
#define _GNU_SOURCE
#include <sys/mman.h>
void *test_map_memory(uintptr_t address, size_t size)
{
  void *p = mmap((void *)address, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  assert(p == (void *)address);
  return p;
}
void test_unmap_memory(void *address, size_t size)
{
  assert(munmap(address, size) == 0);
}
#endif
