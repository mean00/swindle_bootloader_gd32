/*
 * Linker script for Stage 2 (RAM-resident DFU handler).
 * VMA = RAM (0x20000000), LMA = flash (provided via binary embedding).
 *
 * Stage 1 copies the entire Stage 2 image from flash to RAM,
 * so .data initial values are already present in RAM after copy.
 * Stage 2 only needs to zero .bss.
 */

MEMORY
{
  flash (rx) :    ORIGIN = 0x08000000, LENGTH = 0K   /* dummy — no flash access needed */
  ram (rwx) :     ORIGIN = 0x20000000, LENGTH = 20K
}

REGION_ALIAS("REGION_TEXT", ram);
REGION_ALIAS("REGION_RODATA", ram);
REGION_ALIAS("REGION_DATA", ram);
REGION_ALIAS("REGION_BSS", ram);

OUTPUT_FORMAT ("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")

GROUP(libc.a libm.a)

EXTERN(__stm32_vector_table)
EXTERN(stage2_start_c)
EXTERN(stage2_main)
EXTERN(__msp_init)
PROVIDE(__msp_init = ORIGIN(ram) + LENGTH(ram));
PROVIDE(__exc_reset = stage2_entry);
EXTERN(stage2_entry)
ENTRY(stage2_entry)

SECTIONS
{
    .text :
    {
        __text_start__ = .;
        /*
         * Vector table — must be at the very beginning (0x20000000)
         */
        *(.stm32.interrupt_vector)

        /*
         * Program code
         */
        *(.text .text.* .gnu.linkonce.t.*)
        *(.plt)
        *(.gnu.warning)
        *(.glue_7t) *(.glue_7) *(.vfp11_veneer)

        *(.ARM.extab* .gnu.linkonce.armextab.*)
        *(.gcc_except_table)
        *(.eh_frame_hdr)
        *(.eh_frame)

        . = ALIGN(4);
        KEEP(*(.init))

        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP (*(.preinit_array))
        __preinit_array_end = .;

        . = ALIGN(4);
        __init_array_start = .;
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        __init_array_end = .;

        . = ALIGN(0x4);
        KEEP (*crtbegin.o(.ctors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*crtend.o(.ctors))

        . = ALIGN(4);
        KEEP(*(.fini))

        . = ALIGN(4);
        __fini_array_start = .;
        KEEP (*(.fini_array))
        KEEP (*(SORT(.fini_array.*)))
        __fini_array_end = .;

        KEEP (*crtbegin.o(.dtors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
        KEEP (*(SORT(.dtors.*)))
        KEEP (*crtend.o(.dtors))

        . = ALIGN(8);
        __text_end__ = .;
    } > REGION_TEXT

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > REGION_RODATA
    __exidx_end = .;

    .data :
    {
        __data_start__ = .;
        LONG(0)
        . = ALIGN(8);

        *(.got.plt) *(.got)
        *(.data .data.* .gnu.linkonce.d.*)

        . = ALIGN(8);
        __data_end__ = .;
    } > REGION_DATA

    .rodata :
    {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        . = ALIGN(4);
    } > REGION_RODATA

    .bss :
    {
        . = ALIGN(8);
        __bss_start__ = .;
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
        _end = __bss_end__;
    } > REGION_BSS

    /* Debug sections */
    .stab 0 (NOLOAD) : { *(.stab) }
    .stabstr 0 (NOLOAD) : { *(.stabstr) }
    .debug          0 : { *(.debug) }
    .line           0 : { *(.line) }
    .debug_srcinfo  0 : { *(.debug_srcinfo) }
    .debug_sfnames  0 : { *(.debug_sfnames) }
    .debug_aranges  0 : { *(.debug_aranges) }
    .debug_pubnames 0 : { *(.debug_pubnames) }
    .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_loc      0 : { *(.debug_loc) }
    .debug_macinfo  0 : { *(.debug_macinfo) }
    .debug_weaknames 0 : { *(.debug_weaknames) }
    .debug_funcnames 0 : { *(.debug_funcnames) }
    .debug_typenames 0 : { *(.debug_typenames) }
    .debug_varnames  0 : { *(.debug_varnames) }
    .note.gnu.arm.ident 0 : { KEEP (*(.note.gnu.arm.ident)) }
    .ARM.attributes 0 : { KEEP (*(.ARM.attributes)) }
    /DISCARD/ : { *(.note.GNU-stack) }
}