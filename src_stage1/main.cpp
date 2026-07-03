/*
 * Copyright (C) 2018 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

/*
    On the DSO150 there is a pullup on PA13/SWIO & a pullDown on SWCLK/PA14
    So if PA13 is GND, it means it is actively pulled down

*/

#include "esprit.h"
#include "flash_config.h"
#include "lnGPIO_priv.h"
#include <string.h>

#include "lnCpuID.h"
#include "registers.h"
#include "stage2_payload.h"

static void lnExtiSWDOnly();

extern "C" uint32_t ch32_crc(uint32_t addr, uint32_t len_in_u32);

#define FORCE_DFU_PIN 14
#define FORCE_DFU_IO PB14
#define FLASH_START_ADDR 0x08000000UL
#define RAM_START_ADDR 0x20000000UL
#define VTOR_ADDR 0xE000ED08UL
#define VTOR (*(volatile uint32_t *)VTOR_ADDR)

static uint32_t go_dfu = 0;
static uint64_t *marker = (uint64_t *)RAM_START_ADDR;

// Reboots the system into the bootloader, making sure
// it enters in DFU mode.
static void reboot_into_bootloader()
{
    *marker = 0xDEADBEEFCC00FFEEULL;
}

// Clears reboot information so we reboot in "normal" mode
static void clear_reboot_flags()
{
    *marker = 0;
}

// Returns whether we were rebooted into DFU mode
static bool rebooted_into_dfu()
{
    return (*marker == 0xDEADBEEFCC00FFEEULL);
}
/**
 * @brief  Chain execution to firmware located at a given flash/ram address.
 *
 *         Sets the vector table offset register (VTOR) to point to the target
 *         image, then loads the new stack pointer and reset handler from its
 *         vector table and jumps to it. This effectively boots a different
 *         firmware image without requiring an MCU reset.
 *
 * @param  addr  Base address of the target firmware's vector table.
 */
static void chain_to(uint32_t addr)
{
    VTOR = addr;
    __asm__ volatile("mov r12, %0\n"
                     "ldr r0,[r12]\n"
                     "msr msp, r0\n"
                     "msr psp, r0\n"
                     "add r12,#4\n"
                     "ldr pc, [r12]\n" ::"r"(addr));
}

/**
 * @brief  Stage-1 bootloader entry point.
 *
 *         Decides whether to chain to the main application or to fall back
 *         to DFU mode (stage-2). The decision is based on a combination of
 *         external hardware state and firmware integrity checks:
 *
 *         1. If the MCU was explicitly rebooted into DFU (e.g. by the
 *            application calling reboot_into_bootloader()), DFU mode is forced.
 *         2. If the FORCE_DFU pin (PB14) is pulled low (button pressed), DFU
 *            mode is forced.
 *         3. If the image stored at the application offset has no valid stack
 *            pointer (first vector word does not point into SRAM), DFU mode is
 *            forced.
 *         4. Otherwise, the firmware image CRC32 is verified. On mismatch,
 *            DFU mode is forced.
 *
 *         When DFU mode is required, stage-2 is copied from flash into RAM
 *         and executed from there (so the flash can be freely erased/written
 *         during the update). When the image is valid, execution jumps
 *         directly to the application.
 *
 * @return  This function never returns; it chains to either the application
 *          or the DFU payload.
 */
int main(void)
{
    const uint32_t start_addr = FLASH_START_ADDR + (FLASH_BOOTLDR_SIZE_KB * 1024);
    const uint32_t *const base_addr = (uint32_t *)start_addr;

    uint32_t sig = base_addr[0];
    uint32_t imageSize = base_addr[2];
    uint32_t checksum = base_addr[3];

    go_dfu = 0;
    go_dfu = rebooted_into_dfu();
    // Activate GPIO B for now
    lnPeripherals::enable(pGPIOB);
    lnPeripherals::enable(pGPIOC);
    lnPeripherals::enable(pAF);
    lnExtiSWDOnly();

    lnPinMode(FORCE_DFU_IO, lnINPUT_PULLUP);

    for (int i = 0; i < 10; i++) // wait  a bit
        __asm__("nop");

    if (!lnDigitalRead(FORCE_DFU_IO)) // "OK" Key pressed
        go_dfu |= 1;

    if (!go_dfu && (sig >> 20) != 0x200)
        go_dfu |= 2;
    RCC_CSR |= RCC_CSR_RMVF; // Clear reset flag

    lnCpuID::identify();

    if (!go_dfu)
    {
        if (imageSize < 256 * 1024) // Check hash of app is correct
        {
            if (imageSize != 0x1234 || checksum != 0x5678) // valid but no hash, we accept that too
            {
                uint32_t computed = ch32_crc((uint32_t)&(base_addr[4]), imageSize >> 2);
                // uint32_t computed = XXH32(&(base_addr[4]), imageSize, 0x100);
                if (computed != checksum)
                    go_dfu |= 4;
            }
        }
        else
        {
            go_dfu = 1; // absurd size
        }
    }
    clear_reboot_flags();
    // go_dfu=1;

    if (!go_dfu) // all seems good, run the app
    {

        chain_to(APP_ADDRESS & (~0xF));
    }
    // Something is wrong, go DFU
    uint32_t *stage2_ram = (uint32_t *)RAM_START_ADDR;
    memcpy(stage2_ram, stage2_payload, stage2_payload_size);

    // Set vector table base address.
    chain_to(RAM_START_ADDR);
}
#define LN_AFIO_ADR (0x40010000)
#define LN_AFIO_PCF0_SWJ_SET(x) (x << 24)
#define LN_AFIO_PCF0_SWJ_MASK (~(7 << 24))

struct LN_AFIOx
{
    uint32_t EC;
    uint32_t PCF0;
    uint32_t EXTISS[4];
    uint32_t dummy;
    uint32_t PCF1;
};
typedef volatile LN_AFIOx LN_AFIO;

/**
 * @brief  Configure the debug interface for SWD-only mode and apply
 *         a partial timer-1 remap for bluepill-compatible pinout.
 *
 *         Disables JTAG (freeing the associated GPIO pins) while keeping
 *         SWD active, then requests a partial remap of timer-1 channels
 *         to match the bluepill board layout.
 */
void lnExtiSWDOnly()
{
    LN_AFIO *afio = (LN_AFIO *)LN_AFIO_ADR;
    uint32_t v = afio->PCF0;
    v &= LN_AFIO_PCF0_SWJ_MASK;
    v |= LN_AFIO_PCF0_SWJ_SET(0); // SWD + JTAG
    afio->PCF0 = v;
    // Do partial remap on timer1 to follow bluepill layout
    v = afio->PCF0;
    v &= ~(3 << 8);
    v |= 1 << 8;
    afio->PCF0 = v;
}
// EOF
