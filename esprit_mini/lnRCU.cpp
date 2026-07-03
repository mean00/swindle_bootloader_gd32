/*
 *  (C) 2021 MEAN00 fixounet@free.fr
 *  See license file
 */

#include "lnRCU.h"
#include "esprit.h"
#include "lnCpuID.h"
#include "lnPeripheral_priv.h"
#include "lnRCU_priv.h"
//
LN_RCU *arcu = (LN_RCU *)LN_RCU_ADR;
/**
 */
#define RCU_RESET 1
#define RCU_ENABLE 2
#define RCU_DISABLE 3

extern void lnCh32_enableUsbPullUp();
__attribute__((weak)) void lnCh32_enableUsbPullUp()
{
}

static void _rcuAction(const Peripherals periph, int action)
{
    uint32_t enable_bit = 0;
    bool is_apb1 = false;

    switch (periph)
    {
    case pGPIOA: enable_bit = LN_RCU_APB2_PAEN; break;
    case pGPIOB: enable_bit = LN_RCU_APB2_PBEN; break;
    case pGPIOC: enable_bit = LN_RCU_APB2_PCEN; break;
    case pAF:    enable_bit = LN_RCU_APB2_AFEN; break;
    case pUSB:   enable_bit = LN_RCU_APB1_USBDEN; is_apb1 = true; break;
    default:     return; // Unused in this bootloader
    }

    volatile uint32_t *en_reg = is_apb1 ? &(arcu->APB1EN) : &(arcu->APB2EN);
    volatile uint32_t *rst_reg = is_apb1 ? &(arcu->APB1RST) : &(arcu->APB2RST);

    switch (action)
    {
    case RCU_RESET:
        *rst_reg |= enable_bit;
        *rst_reg &= ~enable_bit;
        break;
    case RCU_ENABLE:
        *en_reg |= enable_bit;
        break;
    case RCU_DISABLE:
        *en_reg &= ~enable_bit;
        break;
    }
}

/**
 *
 * @param periph
 */
void lnPeripherals::reset(const Peripherals periph)
{
    _rcuAction(periph, RCU_RESET);
}
/**
 *
 * @param periph
 */
void lnPeripherals::enable(const Peripherals periph)
{
    _rcuAction(periph, RCU_ENABLE);
}
/**
 *
 * @param periph
 */
void lnPeripherals::disable(const Peripherals periph)
{
    _rcuAction(periph, RCU_DISABLE);
}
/**
 *
 * @param divider
 */
void lnPeripherals::setAdcDivider(lnADC_DIVIDER divider)
{
    uint32_t val = arcu->CFG0;

    val &= LN_RCU_ADC_PRESCALER_MASK;
    int r = (int)divider;
    if (r & 4)
    {
        if (lnCpuID::vendor() == lnCpuID::LN_VENDOR_GD) // only up to 8
        {
            val |= LN_RCU_ADC_PRESCALER_HIGHBIT;
            val |= LN_RCU_ADC_PRESCALER_LOWBIT(r & 3);
        }
        else
            val |= LN_RCU_ADC_PRESCALER_LOWBIT(lnADC_CLOCK_DIV_BY_8);
    }
    else
    {
        val |= LN_RCU_ADC_PRESCALER_LOWBIT(r);
    }
    arcu->CFG0 = val;
}
// EOF
