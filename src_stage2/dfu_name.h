#if 1

const char *_usb_strings[5] = {"davidgf.net (libopencm3 based)", // iManufacturer
                               "Swindle bootloader_" VERSION "", // iProduct
                               serial_no,                        // iSerialNumber
                               // Interface desc string
                               /* This string is used by ST Microelectronics' DfuSe utility. */
                               /* Change check_do_erase() accordingly */
                               memory_layout,
                               // Config desc string
                               "Bootloader "};

#else

const char *_usb_strings[5] = {"davidgf.net (libopencm3 based)", // iManufacturer
                               "Taist DFU [" VERSION "]",        // iProduct
                               serial_no,                        // iSerialNumber
                               // Interface desc string
                               /* This string is used by ST Microelectronics' DfuSe utility. */
                               /* Change check_do_erase() accordingly */
                               memory_layout,
                               // Config desc string
                               "Bootloader config: "};

#endif
