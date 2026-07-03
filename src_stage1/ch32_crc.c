#include "stdint.h"

#define LN_CRC_ADR 0x40023000
#define LN_RCU_ADR 0x40021000
#define CH32_CRC32_CONTROL_RESET 1
#define AHBPCENR (*(uint32_t *)(LN_RCU_ADR + 0x14))
#define RCU_CRC_ENABLE (1 << 6)
typedef struct
{
    uint32_t data;
    uint32_t independant_data;
    uint32_t control;
} CRC_IPx;

typedef volatile CRC_IPx CRC_IP;

/**
 * @brief  Compute a CRC32 checksum over a memory region using the hardware CRC peripheral.
 *
 *         Enables the CRC peripheral clock, feeds the given memory region through
 *         the hardware CRC engine, and returns the resulting checksum. This provides
 *         a fast, ROM-efficient way to validate firmware image integrity.
 *
 * @param  addr        Starting address of the data to checksum (must be 32-bit aligned).
 * @param  len_in_u32  Length of the data in 32-bit words.
 * @return             The computed 32-bit CRC value.
 */
uint32_t ch32_crc(uint32_t addr, uint32_t len_in_u32)
{
    // Enable  CRC clock
    AHBPCENR |= RCU_CRC_ENABLE;
    //
    CRC_IP *crc = (CRC_IP *)LN_CRC_ADR;
    // reset writes 0xFFFFFFF by itself
    crc->control = CH32_CRC32_CONTROL_RESET;
    // crc->data = init;
    uint32_t *mem = (uint32_t *)addr;
    uint32_t *lim = mem;
    lim += len_in_u32;
    for (uint32_t *p = mem; p < lim; p++)
    {
        crc->data = *p;
    }
    return (crc->data);
}

// EOF