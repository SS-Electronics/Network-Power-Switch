/*
 * nps_f401cd.ld — Network-Power-Switch linker script for STM32F401CDU6
 * (384 KB flash, 96 KB SRAM). Copy of FreeRTOS-OS
 * arch/devices/STM/STM32F4xx/STM32F411/stm32_f411_FLASH.ld with the memory
 * regions resized: the OS builds this part through its STM32F411xE path
 * (see app/kconfig_f401.conf), whose script assumes 512 KB / 128 KB.
 * Standalone image at 0x08000000 (no bootloader on this board).
 */
/*
File:        stm32_f411_FLASH.ld
Author:      Subhajit Roy
             subhajitroy005@gmail.com

Module:      linker script
Info:        STM32F411VETx — Cortex-M4, 512 KB Flash, 128 KB RAM
             Flat single-region layout: all code + rodata in FLASH,
             .data in RAM (LMA in FLASH).
             --gc-sections removes unreferenced functions/data.

Dependency:  None

This file is part of FreeRTOS-OS Project.

FreeRTOS-OS is free software: you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, either version
3 of the License, or (at your option) any later version.

FreeRTOS-OS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with FreeRTOS-KERNEL. If not, see <https://www.gnu.org/licenses/>.
*/

/*****************************************************************************
*  Target      : STM32F411VETx
*
*  Datasheet   : RM0383 — STM32F411xC/E Reference Manual
*
*  Memory      : Flash 512 KB @ 0x08000000
*              : RAM   128 KB @ 0x20000000
*
*  Flash sectors
*  Sector 0:  0x08000000 – 0x08003FFF  (16 KB)
*  Sector 1:  0x08004000 – 0x08007FFF  (16 KB)
*  Sector 2:  0x08008000 – 0x0800BFFF  (16 KB)
*  Sector 3:  0x0800C000 – 0x0800FFFF  (16 KB)
*  Sector 4:  0x08010000 – 0x0801FFFF  (64 KB)
*  Sector 5:  0x08020000 – 0x0803FFFF  (128 KB)
*  Sector 6:  0x08040000 – 0x0807FFFF  (256 KB)
****************************************************************************/

ENTRY(Reset_Handler)

MINIMUM_MEMORY_MODEL = 1;

_Min_Heap_Size  = 0x200;   /* 512 bytes */
_Min_Stack_Size = 0x400;   /* 1 KB      */

MEMORY
{
  RAM           (xrw) : ORIGIN = 0x20000000, LENGTH = 96K    /* STM32F401xD: 96 KB SRAM  */
  FLASH         (rx)  : ORIGIN = 0x08000000, LENGTH = 384K   /* STM32F401CD: 384 KB flash */
}

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  /* Vector table — must stay at Flash origin; never GC'd */
  .isr_vector ORIGIN(FLASH) :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    KEEP(*(.isr_vector.*))
    . = ALIGN(4);
  } > FLASH

  /* Code and read-only data */
  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    /* crtn.o supplies the _init / _fini epilogs as anonymous .init / .fini
       fragments (no own symbol). Without KEEP, --gc-sections drops them and
       _init falls through into the next code — early BusFault before main. */
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
  } > FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  /* Exception unwind tables */
  .ARM.extab (READONLY) :
  {
    . = ALIGN(4);
    KEEP(*(.ARM.extab* .gnu.linkonce.armextab.*))
    . = ALIGN(4);
  } > FLASH

  .ARM (READONLY) :
  {
    . = ALIGN(4);
    __exidx_start = .;
    KEEP(*(.ARM.exidx*))
    __exidx_end = .;
    . = ALIGN(4);
  } > FLASH

  /* C++ init/fini arrays */
  .preinit_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } > FLASH

  .init_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } > FLASH

  .fini_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } > FLASH

  /* Initialized data: LMA in Flash, VMA in RAM */
  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  /* Uninitialized data (BSS) */
  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start__ = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;
  } > RAM

  /* Heap and stack reservation */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } > RAM

  /* Not-initialised section — placed AFTER heap/stack so it lies outside
   * [__bss_start__, __bss_end__] and is never zeroed by the BSS clear loop.
   * Contents survive NVIC soft resets; used for fault records and safe-state
   * reason persistence. */
  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    __noinit_start = .;
    KEEP(*(.noinit))
    KEEP(*(.noinit.*))
    . = ALIGN(4);
    __noinit_end = .;
  } > RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

PROVIDE(_sidata  = _sidata);
PROVIDE(_sdata   = _sdata);
PROVIDE(_edata   = _edata);
PROVIDE(_sbss    = __bss_start__);
PROVIDE(_ebss    = __bss_end__);
