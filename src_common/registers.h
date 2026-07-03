#pragma once

#define APP_ADDRESS (FLASH_BASE_ADDR + (FLASH_BOOTLDR_SIZE_KB)*1024)

#define RCC_CSR (*(volatile uint32_t *)0x40021024U)
#define RCC_CSR_LPWRRSTF (1 << 31)
#define RCC_CSR_WWDGRSTF (1 << 30)
#define RCC_CSR_IWDGRSTF (1 << 29)
#define RCC_CSR_SFTRSTF (1 << 28)
#define RCC_CSR_PORRSTF (1 << 27)
#define RCC_CSR_PINRSTF (1 << 26)
#define RCC_CSR_RMVF (1 << 24)

#define configTICK_RATE_HZ 1000
#define configSYSTICK_CLOCK_HZ (72 * 1000 * 1000)

#define portNVIC_SYSTICK_CTRL_REG (*((volatile uint32_t *)0xe000e010))
#define portNVIC_SYSTICK_LOAD_REG (*((volatile uint32_t *)0xe000e014))
#define portNVIC_SYSTICK_CURRENT_VALUE_REG (*((volatile uint32_t *)0xe000e018))
#define portNVIC_SHPR3_REG (*((volatile uint32_t *)0xe000ed20))
#define portNVIC_SYSTICK_CLK_BIT (1UL << 2UL)
#define portNVIC_SYSTICK_INT_BIT (1UL << 1UL)
#define portNVIC_SYSTICK_ENABLE_BIT (1UL << 0UL)
