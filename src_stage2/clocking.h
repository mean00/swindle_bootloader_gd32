
#define FLASH_ACR_LATENCY 7
#define FLASH_ACR_LATENCY_2WS 0x02
#define FLASH_ACR (fmc->WS)

#define RCC_CFGR_HPRE_SYSCLK_NODIV 0x0
#define RCC_CFGR_PPRE1_HCLK_DIV2 0x4
#define RCC_CFGR_PPRE2_HCLK_NODIV 0x0
#define RCC_CFGR_ADCPRE_PCLK2_DIV8 0x3
#define RCC_CFGR_PLLMUL_PLL_CLK_MUL9 0x7
#define RCC_CFGR_PLLSRC_HSE_CLK 0x1
#define RCC_CFGR_PLLXTPRE_HSE_CLK 0x0
#define RCC_CFGR_SW_SYSCLKSEL_PLLCLK 0x2
#define RCC_CFGR_SW_SHIFT 0
#define RCC_CFGR_SW (3 << RCC_CFGR_SW_SHIFT)

#define RCC_CR_HSEON (1 << 16)
#define RCC_CR_HSERDY (1 << 17)
#define RCC_CR_PLLON (1 << 24)
#define RCC_CR_PLLRDY (1 << 25)

#define RCC_CR (arcu->CTL)
#define RCC_CFGR (arcu->CFG0)
#define RCC_BCDR (arcu->BDCTL)

/**
 *
 */
#define LN_AFIO_PCF0_SWJ_SET(x) (x << 24)
#define LN_AFIO_PCF0_SWJ_MASK (~(7 << 24))

/**
 * @brief Configure the system clock for 48 MHz operation using the
 *        internal RC oscillator (HSI) as the PLL source.
 *
 * This is the crystal-less clock path for GD32 devices that have an
 * internal 48 MHz RC oscillator (IRC48M) suitable for driving USB.
 *
 * Steps:
 *  - Set AHB/APB prescalers
 *  - Configure PLL: HSI/2 (4 MHz) × 9 = 36 MHz → feeds IRC48M → 48 MHz
 *  - Set flash wait states (2WS for 48-72 MHz)
 *  - Enable PLL and wait for lock
 *  - Switch SYSCLK to PLL output
 *  - Enable and select the dedicated IRC48M oscillator for USB
 */
static void clock_setup_in_hsi_48mhz()
{
    /*
     * Set prescalers for AHB, ADC, ABP1, ABP2.
     * Do this before touching the PLL (TODO: why?).
     */
    uint32_t reg32 = RCC_CFGR & 0xFF00000F;
    reg32 |= (RCC_CFGR_HPRE_SYSCLK_NODIV << 4) | (RCC_CFGR_PPRE1_HCLK_DIV2 << 8) | (RCC_CFGR_PPRE2_HCLK_NODIV << 11) |
             (RCC_CFGR_ADCPRE_PCLK2_DIV8 << 14) | (RCC_CFGR_PLLMUL_PLL_CLK_MUL9 << 18) |
             (RCC_CFGR_PLLSRC_HSE_CLK << 16) | (RCC_CFGR_PLLXTPRE_HSE_CLK << 17);
    RCC_CFGR = reg32;

    // 0WS from 0-24MHz
    // 1WS from 24-48MHz
    // 2WS from 48-72MHz
    FLASH_ACR = (FLASH_ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_2WS;

    /* Enable PLL oscillator and wait for it to stabilize. */
    RCC_CR |= RCC_CR_PLLON;
    while (!(RCC_CR & RCC_CR_PLLRDY))
    {
        __asm__("nop");
    }

    // Select PLL as SYSCLK source.
    RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_SW) | (RCC_CFGR_SW_SYSCLKSEL_PLLCLK << RCC_CFGR_SW_SHIFT);

// Ok now enable the 48Mhz RC oscillator for USB
// This is Gigadevice specific, at least on GD32F303
// It drives the internal 48Mhz clock that can be used
// to drive USB in crystal less setup
#define LN_GD_RCU_ADDCTL_IRC48M_EN (1 << 16)
#define LN_GD_RCU_ADDCTL_IRC48M_STB (1 << 17)
#define LN_GD_RCU_ADDCTL_IRC48M_SEL (1 << 0)

    volatile uint32_t *addctl = (volatile uint32_t *)(LN_RCU_ADR + 0xc0);
    *addctl |= LN_GD_RCU_ADDCTL_IRC48M_EN; // enable 48M
    while (1)
    {
        if (*addctl & LN_GD_RCU_ADDCTL_IRC48M_STB) // clock stabilized
        {
            break;
        }
    }
    // Use IRC48M as usb source
    *addctl |= LN_GD_RCU_ADDCTL_IRC48M_SEL;
}

/**
 * @brief Configure the system clock for 72 MHz operation using an
 *        8 MHz external crystal (HSE) as the PLL source.
 *
 * Steps:
 *  - Enable HSE oscillator and wait for ready
 *  - Set AHB/APB prescalers and PLL multiplier (8 MHz × 9 = 72 MHz)
 *  - Set flash wait states (2WS for 48-72 MHz)
 *  - Enable PLL and wait for lock
 *  - Switch SYSCLK to PLL output
 */
static void clock_setup_in_hse_8mhz_out_72mhz()
{
    // No need to use HSI or HSE while setting up the PLL, just use the RC osc.

    /* Enable external high-speed oscillator 8MHz. */
    RCC_CR |= RCC_CR_HSEON;
    while (!(RCC_CR & RCC_CR_HSERDY))
        ;

    /*
     * Set prescalers for AHB, ADC, ABP1, ABP2.
     * Do this before touching the PLL (TODO: why?).
     */
    uint32_t reg32 = RCC_CFGR & 0xFF00000F;
    reg32 |= (RCC_CFGR_HPRE_SYSCLK_NODIV << 4) | (RCC_CFGR_PPRE1_HCLK_DIV2 << 8) | (RCC_CFGR_PPRE2_HCLK_NODIV << 11) |
             (RCC_CFGR_ADCPRE_PCLK2_DIV8 << 14) | (RCC_CFGR_PLLMUL_PLL_CLK_MUL9 << 18) |
             (RCC_CFGR_PLLSRC_HSE_CLK << 16) | (RCC_CFGR_PLLXTPRE_HSE_CLK << 17);
    RCC_CFGR = reg32;

    // 0WS from 0-24MHz
    // 1WS from 24-48MHz
    // 2WS from 48-72MHz
    FLASH_ACR = (FLASH_ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_2WS;

    /* Enable PLL oscillator and wait for it to stabilize. */
    RCC_CR |= RCC_CR_PLLON;
    while (!(RCC_CR & RCC_CR_PLLRDY))
    {
        __asm__("nop");
    }

    // Select PLL as SYSCLK source.
    RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_SW) | (RCC_CFGR_SW_SYSCLKSEL_PLLCLK << RCC_CFGR_SW_SHIFT);
}
