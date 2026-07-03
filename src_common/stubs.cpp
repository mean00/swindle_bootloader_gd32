#include "esprit.h"
#include "lnIRQ.h"
#include "lnIRQ_arm.h"
#include "lnIRQ_arm_priv.h"
#include "lnNVIC_arm_priv.h"
#include "lnSCB_arm_priv.h"
#include "sys/types.h"
#include "unistd.h"

volatile uint32_t sysTick = 0;

extern "C" void tick(void)
{
    sysTick++;
}

extern "C" void do_assert(const char *msg)
{
    deadEnd(2);
    while (1)
    {
        __asm__("nop");
    }
}

void xDelay(int ms)
{
    uint32_t tgt = sysTick + ms;
    while (1)
    {
        if (sysTick > tgt)
            return;
        __asm__("nop");
    }
}
void lnDelay(uint32_t ms)
{
    xDelay(ms);
}
extern "C" void delay(int ms)
{
    xDelay(ms);
}
/**
 *
 */
void lnDelayUs(int us)
{
    int count = (us * 72) / 6; // appromixate
    for (int i = 0; i < count; i++)
        __asm__("nop");
}
#if 0
extern "C" void deadEnd(int z)
{
    while (1)
    {
    }
}
#endif
bool FMC_hasFastUnlock()
{
    return false;
}
int xstrlen(const char *src)
{
    int nb = 0;
    while (src[nb])
    {
        nb++;
    }
    return nb;
}
void xstrcpy(char *tgt, const char *src)
{
    while (*src)
    {
        *tgt = *src;
        tgt++;
        src++;
    }
    *tgt = 0;
}
void xstrcat(char *tgt, const char *src)
{
    tgt += xstrlen(tgt);
    while (*src)
    {
        *tgt = *src;
        tgt++;
        src++;
    }
    *tgt = 0;
}
#if 0
// Implement this here to save space, quite minimalistic :D
void *memcpy(void *dst, const void *src, size_t count)
{
    uint8_t *dstb = (uint8_t *)dst;
    uint8_t *srcb = (uint8_t *)src;
    while (count--)
        *dstb++ = *srcb++;
    return dst;
}
#endif
extern "C" void _write()
{
    xAssert(0);
}
extern "C" void *_sbrk(ptrdiff_t incr)
{
    xAssert(0);
    return 0;
}
extern "C" void *sbrk(ptrdiff_t incr)
{
    xAssert(0);
    return 0;
}
extern "C" void _putchar(char) {};

#undef printf
#undef scanf

extern "C" int printf(const char *__restrict, ...)
{
    return 0;
}
extern "C" int scanf(const char *__restrict, ...)
{
    return 0;
}

/**

*/
#if 0
void lnDelayUs(int wait)
{
    uint64_t target=lnGetUs()+wait;
    while(1)
    {
        uint64_t vw=lnGetUs();
        if(vw>target)
            return;
        __asm__("nop"::);
    }

}

#endif
extern "C" void Logger_crash(const char *st)
{
    xAssert(0);
}

// EOF
