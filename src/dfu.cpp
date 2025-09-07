

#include "flash.h"
#include "flash_config.h"
#include "lnArduino.h"
#include "lnCpuID.h"
#include "reboot.h"
#include "registers.h"
#include "usb.h"
#include "usb_vid_pid.h"
#include <cstdint>
/* Commands sent with wBlockNum == 0 as per ST implementation. */
#define CMD_SETADDR 0x21
#define CMD_ERASE 0x41

// Payload/app comes inmediately after Bootloader
#define APP_ADDRESS (FLASH_BASE_ADDR + (FLASH_BOOTLDR_SIZE_KB) * 1024)

extern void lcdRun();

// USB control data buffer
uint8_t usbd_control_buffer[DFU_TRANSFER_SIZE];
// DFU state
static enum dfu_state usbdfu_state = STATE_DFU_IDLE;
static struct
{
    uint8_t buf[sizeof(usbd_control_buffer)];
    uint16_t len;
    uint32_t addr;
    uint16_t blocknum;
} prog;

// Serial number to expose via USB
static char serial_no[26];
static char memory_layout[64]; // actual size is ~ 50

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
const char *_usb_strings[5] = {"davidgf.net (libopencm3 based)", // iManufacturer
                               "lnBMP bootloader [" VERSION "]", // iProduct
                               serial_no,                        // iSerialNumber
                               // Interface desc string
                               /* This string is used by ST Microelectronics' DfuSe utility. */
                               /* Change check_do_erase() accordingly */
                               memory_layout,
                               // Config desc string
                               "Bootloader config: "
#ifdef ENABLE_WATCHDOG
                               "WtDg[" STR(ENABLE_WATCHDOG) "s] "
#endif
#ifdef ENABLE_SAFEWRITE
                                                            "SafeWr "
#endif
#ifdef ENABLE_PROTECTIONS
                                                            "RDO/DBG "
#endif
#ifdef ENABLE_CHECKSUM
                                                            "FW-CRC "
#endif
};
extern void xstrcpy(char *tgt, const char *src);
extern void xstrcat(char *tgt, const char *src);
static void prepare_memory_layout()
{
    int flash_size = lnCpuID::flashSize();
    const char *s;
    switch (flash_size)
    {
    case 64:
        s = "52";
        break; // 64-8-4
    case 256:
        s = "244";
        break; // 64-8-4
    default:
    case 128:
        s = "116";
        break; // 64-8-4
    }

    xstrcpy(memory_layout, "@Internal Flash /0x08000000/");
    xstrcat(memory_layout, BOOTLOADER_SIZE_STR);
    xstrcat(memory_layout, "*001Ka,");
    xstrcat(memory_layout, s); //"116");
    xstrcat(memory_layout, "*001Kg");
}
static const char hcharset[16 + 1] = "0123456789abcdef";
static void get_dev_unique_id(char *s)
{
    volatile uint8_t *unique_id = (volatile uint8_t *)0x1FFFF7E8;
    s[0] = 'L';
    s[1] = 'N';
    s[2] = 'B';
    s[3] = 'M';
    s[4] = 'P';
    s[5] = '_';
    /* Fetch serial number from chip's unique ID */
    for (int i = 6; i < 24; i += 2)
    {
        uint8_t cid = *unique_id++;
        s[i] = hcharset[(cid >> 4) & 0xF];
        s[i + 1] = hcharset[cid & 0xF];
    }
    s[24] = 0;
    s[25] = 0;
}

static uint8_t usbdfu_getstatus(uint32_t *bwPollTimeout)
{
    switch (usbdfu_state)
    {
    case STATE_DFU_DNLOAD_SYNC:
        usbdfu_state = STATE_DFU_DNBUSY;
        *bwPollTimeout = 100;
        return DFU_STATUS_OK;
    case STATE_DFU_MANIFEST_SYNC:
        // Device will reset when read is complete.
        usbdfu_state = STATE_DFU_MANIFEST;
        return DFU_STATUS_OK;
    case STATE_DFU_ERROR:
        return STATE_DFU_ERROR;
    default:
        return DFU_STATUS_OK;
    }
}

// GPIO/RCC stuff

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018U)

#define rcc_gpio_enable(gpion) RCC_APB2ENR |= (1 << (gpion + 2));
void runDfu();
void lcdRun();
static void usbdfu_getstatus_complete(struct usb_setup_data *req)
{
    (void)req;

    // Protect the flash by only writing to the valid flash area
    const int flash_size = lnCpuID::flashSize(); // in kB
    const uint32_t start_addr = 0x08000000 + (FLASH_BOOTLDR_SIZE_KB * 1024);
    const uint32_t end_addr = 0x08000000 + (flash_size * 1024);

    switch (usbdfu_state)
    {
    case STATE_DFU_DNBUSY:
        _flash_unlock(0);
        if (prog.blocknum == 0)
        {
            switch (prog.buf[0])
            {
            case CMD_ERASE: {
#ifdef ENABLE_SAFEWRITE
                check_do_erase();
#endif

                // Clear this page here.
                uint32_t baseaddr = *(uint32_t *)(prog.buf + 1);
                if (baseaddr >= start_addr && baseaddr + DFU_TRANSFER_SIZE <= end_addr)
                {
                    if (!_flash_page_is_erased(baseaddr))
                        _flash_erase_page(baseaddr);
                }
            }
            break;
            case CMD_SETADDR:
                // Assuming little endian here.
                prog.addr = *(uint32_t *)(prog.buf + 1);
                break;
            }
        }
        else
        {
#ifdef ENABLE_SAFEWRITE
            check_do_erase();
#endif

            // From formula Address_Pointer + ((wBlockNum - 2)*wTransferSize)
            uint32_t baseaddr = prog.addr + ((prog.blocknum - 2) * DFU_TRANSFER_SIZE);

            if (baseaddr >= start_addr && baseaddr + prog.len <= end_addr)
            {
                // Program buffer in one go after erasing.
                if (!_flash_page_is_erased(baseaddr))
                    _flash_erase_page(baseaddr);
                _flash_program_buffer(baseaddr, (uint16_t *)prog.buf, prog.len);
            }
        }
        _flash_lock();

        /* Jump straight to dfuDNLOAD-IDLE, skipping dfuDNLOAD-SYNC. */
        usbdfu_state = STATE_DFU_DNLOAD_IDLE;
        return;
    case STATE_DFU_MANIFEST:
        return; // Reset placed in main loop.
    default:
        return;
    }
}

enum usbd_request_return_codes usbdfu_control_request(struct usb_setup_data *req, uint16_t *len,
                                                      void (**complete)(struct usb_setup_data *req))
{
    switch (req->bRequest)
    {
    case DFU_DNLOAD:
        if ((len == NULL) || (*len == 0))
        {
            // wLength = 0 means leave DFU
            usbdfu_state = STATE_DFU_MANIFEST_SYNC;
            return USBD_REQ_HANDLED;
        }
        else
        {
            /* Copy download data for use on GET_STATUS. */
            prog.blocknum = req->wValue;
            // Beware overflows!
            prog.len = *len;
            if (prog.len > sizeof(prog.buf))
                prog.len = sizeof(prog.buf);
            memcpy(prog.buf, usbd_control_buffer, prog.len);
            usbdfu_state = STATE_DFU_DNLOAD_SYNC;
            return USBD_REQ_HANDLED;
        }
    case DFU_CLRSTATUS:
        // Just clears errors.
        if (usbdfu_state == STATE_DFU_ERROR)
            usbdfu_state = STATE_DFU_IDLE;
        return USBD_REQ_HANDLED;
    case DFU_ABORT:
        // Abort just returns to IDLE state.
        usbdfu_state = STATE_DFU_IDLE;
        return USBD_REQ_HANDLED;
    case DFU_DETACH:
        usbdfu_state = STATE_DFU_MANIFEST;
        return USBD_REQ_HANDLED;
    case DFU_UPLOAD:
        // Send data back to host by reading the image.
        usbdfu_state = STATE_DFU_UPLOAD_IDLE;
        if (!req->wValue)
        {
            // Send back supported commands.
            usbd_control_buffer[0] = 0x00;
            usbd_control_buffer[1] = CMD_SETADDR;
            usbd_control_buffer[2] = CMD_ERASE;
            *len = 3;
            return USBD_REQ_HANDLED;
        }
        else
        {
// Send back data if only if we enabled that.
#ifndef ENABLE_DFU_UPLOAD
            usbdfu_state = STATE_DFU_ERROR;
            *len = 0;
#else
            // From formula Address_Pointer + ((wBlockNum - 2)*wTransferSize)
            uint32_t baseaddr = prog.addr + ((req->wValue - 2) * DFU_TRANSFER_SIZE);
            const uint32_t start_addr = 0x08000000 + (FLASH_BOOTLDR_SIZE_KB * 1024);
            const uint32_t end_addr = 0x08000000 + (FLASH_SIZE_KB * 1024);
            if (baseaddr >= start_addr && baseaddr + DFU_TRANSFER_SIZE <= end_addr)
            {
                memcpy(usbd_control_buffer, (void *)baseaddr, DFU_TRANSFER_SIZE);
                *len = DFU_TRANSFER_SIZE;
            }
            else
            {
                usbdfu_state = STATE_DFU_ERROR;
                *len = 0;
            }
#endif
        }
        return USBD_REQ_HANDLED;
    case DFU_GETSTATUS: {
        // Perfom the action and register complete callback.
        uint32_t bwPollTimeout = 0; /* 24-bit integer in DFU class spec */
        usbd_control_buffer[0] = usbdfu_getstatus(&bwPollTimeout);
        usbd_control_buffer[1] = bwPollTimeout & 0xFF;
        usbd_control_buffer[2] = (bwPollTimeout >> 8) & 0xFF;
        usbd_control_buffer[3] = (bwPollTimeout >> 16) & 0xFF;
        usbd_control_buffer[4] = usbdfu_state;
        usbd_control_buffer[5] = 0; /* iString not used here */
        *len = 6;
        *complete = usbdfu_getstatus_complete;
        return USBD_REQ_HANDLED;
    }
    case DFU_GETSTATE:
        // Return state with no state transision.
        usbd_control_buffer[0] = usbdfu_state;
        *len = 1;
        return USBD_REQ_HANDLED;
    }

    return USBD_REQ_NEXT_CALLBACK;
}

static void _full_system_reset()
{
    // Reset and wait for it!
    volatile uint32_t *_scb_aircr = (uint32_t *)0xE000ED0CU;
    *_scb_aircr = 0x05FA0000 | 0x4;
    while (1)
        ;
    __builtin_unreachable();
}

#define FLASH_ACR_LATENCY 7
#define FLASH_ACR_LATENCY_2WS 0x02
#define FLASH_ACR (*(volatile uint32_t *)0x40022000U)

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
#define RCC_CR (*(volatile uint32_t *)0x40021000U)
#define RCC_CFGR (*(volatile uint32_t *)0x40021004U)
#define RCC_BCDR (*(volatile uint32_t *)0x40021020U)
#define PWR (volatile uint32_t *)(0x40000000U + 0x7000U)
#define LN_AFIO_ADR (0x40010000)

/**
 *
 */
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

// Arm version
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
/**
 * @brief
 *
 */
void clock_setup_in_hsi_48mhz()
{
    /*
     * Set prescalers for AHB, ADC, ABP1, ABP2.
     * Do this before touching the PLL (TODO: why?).
       Use RCI /2 as pll input = 4Mhz
     */
    uint32_t reg32 = RCC_CFGR & 0xFFC0000F;
    reg32 |= (RCC_CFGR_HPRE_SYSCLK_NODIV << 4) | (RCC_CFGR_PPRE1_HCLK_DIV2 << 8) | (RCC_CFGR_PPRE2_HCLK_NODIV << 11) |
             (RCC_CFGR_PLLMUL_PLL_CLK_MUL9 << 18) | (RCC_CFGR_PLLXTPRE_HSE_CLK << 17);
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
#define LN_RCU_ADR (0x40021000)
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
 * @brief
 *
 */
void clock_setup_in_hse_8mhz_out_72mhz()
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
    uint32_t reg32 = RCC_CFGR & 0xFFC0000F;
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
#if 0
	// Disable external 32k
    *PWR = 1<<8;
	__asm__("nop");
	__asm__("nop");
	RCC_BCDR |= 1<<16; // reset
	__asm__("nop");
	__asm__("nop");
	RCC_BCDR=0;
#endif
}
/**
 *
 */
extern volatile uint32_t sysTick;
void setupForUsb()
{
#ifdef USE_GD32_CRYSTALLESS
    clock_setup_in_hsi_48mhz();
#else
    clock_setup_in_hse_8mhz_out_72mhz();
#endif
    lnPeripherals::enable(pGPIOA);
    lnPeripherals::enable(pGPIOB);
    lnPeripherals::enable(pGPIOC);
    lnPeripherals::enable(pAF);
    // start tick interrupt
    sysTick = 0;
    portNVIC_SYSTICK_CTRL_REG = 0UL;
    portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;

    /* Configure SysTick to interrupt at the requested rate. */
    portNVIC_SYSTICK_LOAD_REG = (configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ) - 1UL;
    portNVIC_SYSTICK_CTRL_REG = (portNVIC_SYSTICK_CLK_BIT | portNVIC_SYSTICK_INT_BIT | portNVIC_SYSTICK_ENABLE_BIT);

    /* Disable USB peripheral as it overrides GPIO settings */
    *USB_CNTR_REG = USB_CNTR_PWDN;
}
/**
 *
 */
extern volatile uint32_t sysTick;
#ifdef USE_GD32_CRYSTALLESS
#define LED PA0
#else
#define LED PC13
#endif
#define LED2 PA8
#define LED3 PB13
volatile int nextTick = 0;
void runDfu()
{

    /*
     * Vile hack to reenumerate, physically _drag_ d+ low.
     * (need at least 2.5us to trigger usb disconnect)
     */
    lnPinMode(PA12, lnOUTPUT);
    // lnPinMode(PA11,lnINPUT_PULLDOWN);
    // xDelay(1000);
    // lnPinMode(PA12,lnOUTPUT);
    // lnDigitalWrite(PA12,1);
    lnDigitalWrite(PA12, 0);
    xDelay(300);
    lnDigitalWrite(PA12, 1);

    get_dev_unique_id(serial_no);
    prepare_memory_layout();
    usb_init();

    lnPinMode(LED, lnOUTPUT);
    lnPinMode(LED2, lnOUTPUT);
    lnPinMode(LED3, lnOUTPUT);
    bool led = false;
    nextTick = sysTick + 100;
    while (1)
    {
        // Poll based approach
        do_usb_poll();
        if (usbdfu_state == STATE_DFU_MANIFEST)
        {
            // USB device must detach, we just reset...
            _full_system_reset();
        }
        if (sysTick > nextTick)
        {
            nextTick += 100;
            lnDigitalWrite(LED, led);
            lnDigitalWrite(LED2, led);
            lnDigitalWrite(LED3, led);
            led = !led;
        }
    }
}
