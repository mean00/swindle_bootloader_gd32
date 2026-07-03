#include "esprit.h"
#include "lnCpuID.h"
#include "lnSCB_arm_priv.h"

static lnCpuID::LN_MCU _mcu;
static lnCpuID::LN_VENDOR _vendor;
static uint32_t _cpuId = 0;
static int _flashSize = 0;
static int _ramSize = 0;
extern LN_SCB_Registers *aSCB;

enum MCU_IDENTIFICATION
{
    MCU_NONE,
    MCU_STM32_64K,
    MCU_STM32_128K,
    MCU_GD32_64K,
    MCU_GD32_128K,
    MCU_GD32F3_256K
};

static MCU_IDENTIFICATION _chipId;
uint32_t MVFR0;
uint32_t MVFR1;

/**
 *
 */
void lnCpuID::identify()
{

    if (_flashSize)
        return; // already done

    _cpuId = aSCB->CPUID;

    uint32_t density = *(uint32_t *)0x1FFFF7E0;
    int ramSize = density >> 16;
    _flashSize = density & 0xffff;
    // If we have the ram size, it is a gd32 chip. Do something better...
    if (ramSize != 0xffff) // GD
    {
        _vendor = LN_VENDOR_GD;
    }
    else // STM32, we dont have the ram size
    {
        _vendor = LN_VENDOR_STM;
    }
}
/**
 *
 * @return
 */
lnCpuID::LN_VENDOR lnCpuID::vendor()
{
    return _vendor;
}
/**
 *
 * @return
 */
lnCpuID::LN_MCU lnCpuID::mcu()
{
    return _mcu;
}
/**
 *
 * @return
 */
int lnCpuID::flashSize()
{
    return _flashSize;
}
/**
 *
 * @return
 */
int lnCpuID::ramSize()
{
    return _ramSize;
}
/**
 *
 * @return
 */
int lnCpuID::clockSpeed()
{
    return SystemCoreClock;
}
