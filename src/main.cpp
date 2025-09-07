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

#include "flash.h"
#include "flash_config.h"
#include "lnArduino.h"
#include "reboot.h"
#include "usb.h"
#include "watchdog.h"
// #include "xxhash.h"
#include <string.h>

#include "lnCpuID.h"
#include "registers.h"

void runDfu();
void setupForUsb();
void lnExtiSWDOnly();

#define FORCE_DFU_IO PB14
extern "C" uint32_t ch32_crc(uint32_t addr, uint32_t len_in_u32);

uint32_t go_dfu = 0;
#define FLASH_START_ADDR 0x08000000UL

int main(void)
{
    {
        volatile uint32_t *_csb_vtor = (uint32_t *)0xE000ED08U;
        *_csb_vtor = FLASH_START_ADDR;
    }
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

        // Set vector table base address.
        volatile uint32_t *_csb_vtor = (uint32_t *)0xE000ED08U;
        *_csb_vtor = APP_ADDRESS & (~0xF);
        // Initialise master stack pointer.
        __asm__ volatile("ldr r12, %0\n"
                         "msr msp, r12\n" ::"g"(*(volatile uint32_t *)APP_ADDRESS));
        // Jump to application.
        (*(void (**)())(APP_ADDRESS + 4))();
    }
    // Something is wrong , go DFU
    setupForUsb();
    runDfu();
}
/**
 * @brief
 *
 */
int is_gd32(void)
{
    return (lnCpuID::vendor() == lnCpuID::LN_MCU_GD32);
}
