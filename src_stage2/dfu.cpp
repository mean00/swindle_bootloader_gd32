#include "esprit.h"
#include "flash.h"
#include "flash_config.h"
#include "lnCpuID.h"
#include "lnSystemTime.h"
// #include "reboot.h"
#include "lnRCU.h"
#include "lnRCU_priv.h"
#include "registers.h"
#include "usb.h"
#include "usb_vid_pid.h"
#include <cstdint>

/** Read a 32-bit little-endian value from any (possibly unaligned) address. */
static inline uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @defgroup dfu_commands ST DfuSe command codes
 *
 * These commands are embedded in the first byte of a DFU_DNLOAD payload
 * when wBlockNum == 0, following the ST Microelectronics DfuSe extension.
 * @{ */
#define CMD_SETADDR 0x21 /**< Set the base address for subsequent transfers. */
#define CMD_ERASE 0x41   /**< Erase a flash page at the address in bytes 1..4. */
/** @} */

#ifdef USE_GD32_CRYSTALLESS
#define LED PA0
#else
#define LED PC13
#endif
#define LED2 PA8
#define LED3 PB13
/**
 * @brief Buffer for USB control transfer data, sized to hold one full DFU
 *        transfer payload.
 *
 * Shared between the USB stack and the DFU application layer.
 * Declared here, referenced as extern in usb.cpp.
 */
uint8_t usbd_control_buffer[DFU_TRANSFER_SIZE];

/**
 * @brief State for the ongoing DFU program operation.
 *
 * Holds a shadow copy of the download payload and metadata so that the
 * actual flash write/erase can be deferred to the GET_STATUS completion
 * callback (as required by the DFU spec).
 */
static struct
{
    uint8_t buf[sizeof(usbd_control_buffer)]; /**< Shadow copy of download data. */
    uint16_t len;                             /**< Number of valid bytes in buf. */
    uint32_t addr;                            /**< Target flash address (set via CMD_SETADDR). */
    uint16_t blocknum;                        /**< Current block number from host. */
} prog;
//
extern void lcdRun();
extern void xstrcpy(char *tgt, const char *src);
extern void xstrcat(char *tgt, const char *src);
//
static bool gd32 = false;
volatile int nextTick = 0;
extern volatile uint32_t sysTick;
extern LN_RCU *arcu;

extern "C" uint32_t __bss_start__, __bss_end__;

/**
 * @brief Current DFU state machine state.
 *
 * Initialised to STATE_DFU_IDLE and advanced by DFU class requests and
 * internal processing (e.g. after GET_STATUS completes a write).
 */
static enum dfu_state usbdfu_state = STATE_DFU_IDLE;
/**
 * @brief Buffer for the USB serial-number string, built from the chip unique ID.
 *
 * Formatted as "LNBMP_" followed by 18 hex digits of the 96-bit UID.
 */
static char serial_no[26];

/**
 * @brief Buffer for the memory-layout descriptor exposed via USB.
 *
 * Formatted for ST DfuSe utility compliance, e.g.
 * "\@Internal Flash /0x08000000/08*001Ka,116*001Kg".
 * The first sector range covers the bootloader area (read-only),
 * the second covers the user application area.
 */
static char memory_layout[64]; // actual size is ~ 50

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#include "dfu_name.h"
//
static int is_gd32(void)
{
    return (lnCpuID::vendor() == lnCpuID::LN_VENDOR_GD);
}

/**
 * @brief Build the DfuSe memory-layout string and store it in the global
 *        memory_layout buffer.
 *
 * The layout advertises the sectors available for DFU operations to the
 * ST DfuSe host utility. The bootloader area is always reported as
 * read-only and the remaining flash as programmable.
 *
 * The available user-flash size is computed from the chip's flashSize()
 * minus the bootloader reservation.
 */
static void prepare_memory_layout()
{
    int flash_size = lnCpuID::flashSize();
    int user_kb = flash_size - FLASH_BOOTLDR_SIZE_KB;

    xstrcpy(memory_layout, "@Internal Flash /0x08000000/");
    xstrcat(memory_layout, BOOTLOADER_SIZE_STR);
    xstrcat(memory_layout, "*001Ka,");

    /* Convert user_kb to decimal string. */
    char num[8];
    char *p = num + sizeof(num) - 1;
    *p = 0;
    do
    {
        *--p = '0' + (user_kb % 10);
        user_kb /= 10;
    } while (user_kb);
    xstrcat(memory_layout, p);
    xstrcat(memory_layout, "*001Kg");
}

/**
 * @brief Hex character lookup table used to format the chip unique ID.
 */
static const char hcharset[16 + 1] = "0123456789abcdef";

/**
 * @brief Build a human-readable serial-number string from the MCU's
 *        unique 96-bit device ID.
 *
 * The serial is formatted as "LNBMP_" followed by 18 hex digits
 * (the 9-byte UID read from 0x1FFFF7E8 on GD32/STM32).
 *
 * @param[out] s  Buffer of at least 26 bytes to receive the NUL-terminated string.
 */
static void get_dev_unique_id(char *s)
{
    volatile uint8_t *unique_id = (volatile uint8_t *)0x1FFFF7E8;
    s[0] = 'S';
    s[1] = 'W';
    s[2] = 'I';
    s[3] = 'N';
    s[4] = 'D';
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

/**
 * @brief Handle the DFU_GETSTATUS request by advancing the DFU state.
 *
 * The DFU spec defers write/erase execution from DFU_DNLOAD to
 * DFU_GETSTATUS. This function performs the state transition and
 * communicates the poll timeout to the host.
 *
 * @param[out] bwPollTimeout  Poll timeout in ms (24-bit value per DFU spec).
 * @return DFU_STATUS_OK on success, DFU_STATUS_ERR_* otherwise.
 */
static enum dfu_status usbdfu_getstatus(uint32_t *bwPollTimeout)
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
        // STATE_DFU_ERROR;
        return DFU_STATUS_ERR_UNKNOWN;
    default:
        return DFU_STATUS_OK;
    }
}

// GPIO/RCC stuff

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018U)

#define rcc_gpio_enable(gpion) RCC_APB2ENR |= (1 << (gpion + 2));
void lcdRun();

/**
 * @brief Callback invoked when the GET_STATUS status stage completes.
 *
 * Performs the actual flash write or erase operation that was scheduled
 * during DFU_DNLOAD. The operation is executed only when the target
 * address falls within the valid application area (after the bootloader
 * region and within the chip's flash size).
 *
 * @param req  Pointer to the original USB setup data (unused).
 */
static void usbdfu_getstatus_complete(struct usb_setup_data *req)
{
    (void)req;

    // Protect the flash by only writing to the valid flash area
    const int flash_size = lnCpuID::flashSize(); // in kB
    const uint32_t start_addr = FLASH_BASE_ADDR + (FLASH_BOOTLDR_SIZE_KB * 1024);
    const uint32_t end_addr = FLASH_BASE_ADDR + (flash_size * 1024);

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
                uint32_t baseaddr = read_u32(prog.buf + 1);
                if (baseaddr >= start_addr && baseaddr + DFU_TRANSFER_SIZE <= end_addr)
                {
                    if (!_flash_page_is_erased(baseaddr))
                        _flash_erase_page(baseaddr);
                }
            }
            break;
            case CMD_SETADDR:
                // Assuming little endian here.
                prog.addr = read_u32(prog.buf + 1);
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
                // Erase each hardware page in the transfer range, then
                // program the full chunk in one go.
                for (uint32_t page = baseaddr; page < baseaddr + prog.len; page += FLASH_PAGE_SIZE)
                    if (!_flash_page_is_erased(page))
                        _flash_erase_page(page);
                if (gd32)
                    _flash_program_buffer_gd32(baseaddr, (uint16_t *)prog.buf, prog.len);
                else
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

/**
 * @brief Main entry point for DFU class-specific USB control requests.
 *
 * Handles all DFU requests (DNLOAD, UPLOAD, GETSTATUS, CLRSTATUS,
 * GETSTATE, ABORT, DETACH) and translates them into DFU state machine
 * transitions.
 *
 * @param req       The decoded USB setup packet.
 * @param len       Pointer to the data-stage length (in/out).
 * @param complete  Output parameter: set to a callback invoked when the
 *                  status stage completes (used by GETSTATUS for deferred
 *                  flash operations).
 * @return USBD_REQ_HANDLED, USBD_REQ_NOTSUPP, or USBD_REQ_NEXT_CALLBACK.
 */
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
            const uint32_t start_addr = FLASH_BASE_ADDR + (FLASH_BOOTLDR_SIZE_KB * 1024);
            const uint32_t end_addr = FLASH_BASE_ADDR + (lnCpuID::flashSize() * 1024);
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

/**
 * @brief Perform a full system reset using the SCB AIRCR register.
 *
 * Writes the reset vector key (0x05FA) and SYSRESETREQ bit to the
 * Application Interrupt and Reset Control Register. Loops forever
 * waiting for the reset to take effect.
 */
static void _full_system_reset()
{
    // Reset and wait for it!
    volatile uint32_t *_scb_aircr = (uint32_t *)0xE000ED0CU;
    *_scb_aircr = 0x05FA0000 | 0x4;
    while (1)
        ;
    __builtin_unreachable();
}

#include "clocking.h"

/**
 * @brief Initialise clocks and basic peripherals required before USB
 *        enumeration can begin.
 *
 * Selects the clock path (crystal-less HSI 48 MHz or HSE 72 MHz) based
 * on the USE_GD32_CRYSTALLESS compile flag, enables GPIO and AF clocks,
 * starts the SysTick timer for delay/millisecond tracking, and disables
 * the USB peripheral so that GPIO overrides do not interfere.
 */
static void setupForUsb()
{
    if (is_gd32())
        gd32 = true;
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

    /* Enable USB clock so we can write to USB registers */
    lnPeripherals::enable(pUSB);

    /* Disable USB peripheral as it overrides GPIO settings */
    *USB_CNTR_REG = USB_CNTR_PWDN;
}
/**
 * @brief Main DFU application entry point.
 *
 * Performs a USB re-enumeration by pulling the D+ line low for 300 ms,
 * then initialises the serial number, memory layout string, and USB
 * hardware. Enters a polling loop that processes USB events and
 * blinks status LEDs. When the DFU state machine reaches MANIFEST
 * (triggered by DFU_DETACH or zero-length DNLOAD), a full system
 * reset is performed to boot the newly written application.
 */
static void runDfu()
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
    lnDelayMs(300);
    lnDigitalWrite(PA12, 1);
    lnPinMode(PA12, lnINPUT_FLOATING);

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
/*
 *
 */
extern "C" void stage2_entry()
{
    // Zero BSS
    uint32_t *bss = &__bss_start__;
    while (bss < &__bss_end__)
    {
        *bss++ = 0;
    }

    lnCpuID::identify();
    // Initialize enough for USB
    setupForUsb();
    runDfu();
}
