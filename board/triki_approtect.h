// Keep SWD open on nRF52 revisions with hardened APPROTECT.
// On these chips the debug port locks at every reset unless UICR.APPROTECT
// is HwDisabled (0x5A, written by the bootloader hex) AND firmware writes
// APPROTECT.DISABLE = SwDisable on every boot. MDK 8.40 (nRF5 SDK 17.1) does
// not do the second part yet, so we do it ourselves as early as possible.
#ifndef TRIKI_APPROTECT_H
#define TRIKI_APPROTECT_H

#include "nrf.h"

#define TRIKI_UICR_APPROTECT        (*(volatile uint32_t *) 0x10001208)
#define TRIKI_APPROTECT_DISABLE     (*(volatile uint32_t *) 0x40000558)
#define TRIKI_APPROTECT_HW_DISABLED 0x5A
#define TRIKI_APPROTECT_SW_DISABLE  0x5A

static inline void triki_keep_debug_open(void)
{
    if ((TRIKI_UICR_APPROTECT & 0xFF) == TRIKI_APPROTECT_HW_DISABLED)
        TRIKI_APPROTECT_DISABLE = TRIKI_APPROTECT_SW_DISABLE;
}

#endif
