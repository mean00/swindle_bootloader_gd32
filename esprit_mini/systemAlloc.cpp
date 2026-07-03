/*
 *  (C) 2021 MEAN00 fixounet@free.fr
 *  See license file
 */
#include "stdint.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

uint32_t *fakeHeap = (uint32_t *)(0x20000000 + (20 - 10) * 1024);

extern "C"
{
    void do_assert(const char *a);
    void *sysAlloc(int size)
    {
        int rounded = ((size + 3) >> 2) << 2;
        fakeHeap -= rounded;
        uint8_t *z = (uint8_t *)fakeHeap;
        for (int i = 0; i < size; i++)
            z[i] = 0;
        return fakeHeap;
    }
    void sysFree(void *p)
    {
    }
    //
    void *calloc(size_t n, size_t elemSize)
    {
        return sysAlloc(n * elemSize);
    }
    void *malloc(size_t n)
    {
        return sysAlloc(n);
    }
    void free(void *p)
    {
    }
}

// C++ part
void *operator new(size_t size)
{
    return sysAlloc(size);
}
void *operator new[](size_t size)
{
    return sysAlloc(size);
}

void operator delete(void *p)
{
    sysFree(p);
}
void operator delete(void *p, unsigned int n)
{
    sysFree(p);
}
void operator delete[](void *p)
{
    sysFree(p);
}
// EOF