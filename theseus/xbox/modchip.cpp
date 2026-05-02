// modchip.cpp: modchip detection and LPC bus probes. Identifies
// Xenium / Aladdin / OpenXenium variants by reading their distinctive
// register signatures. Theseus-original.

#include "std.h"

#ifdef _XBOX
#include "xkflash.h"
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#define LPC_MEMORY_BASE 0xFF000000u
#define XENIUM_REGISTER_BANKING 0x00EF

#define XENIUM_MANUF_ID   0x01
#define XENIUM_MANUF_ID1  0xC2
#define XENIUM_DEVICE_ID  0xC4

static void xenium_reset() {
    volatile uint8_t* lpc = (uint8_t*)LPC_MEMORY_BASE;
    lpc[0xAAA] = 0xAA;
    lpc[0x555] = 0x55;
    lpc[0xAAA] = 0xF0;
}

static uint8_t xenium_read_flash(uint32_t address) {
    volatile uint8_t* lpc = (uint8_t*)LPC_MEMORY_BASE;
    return lpc[address];
}

static bool xenium_detect_chip() {
    volatile uint8_t* lpc = (uint8_t*)LPC_MEMORY_BASE;

    // Enter JEDEC ID mode
    lpc[0xAAA] = 0xAA;
    lpc[0x555] = 0x55;
    lpc[0xAAA] = 0x90;

    uint8_t manuf = xenium_read_flash(0x00);
    uint8_t dev   = xenium_read_flash(0x02);

    // Exit ID mode
    xenium_reset();

    // Dummy read to re-stabilize chip state
    xenium_read_flash(0x00);

    return ((manuf == XENIUM_MANUF_ID || manuf == XENIUM_MANUF_ID1) && dev == XENIUM_DEVICE_ID);
}

const TCHAR* DetectModchip()
{
    static TCHAR szModchip[64] = _T("Unknown");
    static bool initialized = false;

    if (initialized)
        return szModchip;
    initialized = true;

    // Check for Xenium first via CPLD probing
    if (xenium_detect_chip()) {
        _tcscpy(szModchip, _T("Xenium (OpenXenium)"));
        return szModchip;
    }

    // Flash ID fallback
    CXBoxFlash flash;
    flash.AddFCI(0x09, 0x00, "TSOP (Protected)", 0x00000);
    flash.AddFCI(0x01, 0xAD, "Xecuter 3", 0x100000);
    flash.AddFCI(0x01, 0xD5, "Xecuter 2", 0x100000);
    flash.AddFCI(0x01, 0xC4, "Xenium", 0x100000);
    flash.AddFCI(0x04, 0xBA, "ALX2+ R3", 0x40000);

    fci_t* result1 = flash.CheckID();
    fci_t* result2 = flash.CheckID2();

    const char* name = "Unknown";

    if (result1 && result2)
        name = (strcmp(result1->text, result2->text) == 0) ? result1->text : result2->text;
    else if (result1)
        name = result1->text;
    else if (result2)
        name = result2->text;

    _stprintf(szModchip, _T("%S"), name);
    return szModchip;
}

#else // desktop: no LPC bus, no flash chip to probe.

const TCHAR* DetectModchip()
{
    return _T("N/A (Desktop)");
}

#endif // _XBOX
