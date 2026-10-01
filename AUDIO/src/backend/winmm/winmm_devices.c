// ═══════════════════════════════════════════════════════════════════════════
//  backend/winmm/winmm_devices.c — device enumeration, names, default device
// ═══════════════════════════════════════════════════════════════════════════

#include "backend/winmm/winmm_backend.h"

#include <string.h>

// From mmddk.h (the driver kit), which not every SDK ships.
#define AT_DRVM_MAPPER_PREFERRED_GET (0x2000 + 21)

// The device the wave mapper sends audio to — what the user chose in the sound
// control panel. Windows 98 SE / 2000 and later answer this; where it is not
// supported we return WAVE_MAPPER, which still PLAYS on the default device, we
// just cannot name it.
UINT winmm_default_device(void) {
    DWORD device = (DWORD)-1;
    DWORD flags  = 0;
    MMRESULT mmr = waveOutMessage((HWAVEOUT)(INT_PTR)-1, AT_DRVM_MAPPER_PREFERRED_GET,
                                  (DWORD_PTR)&device, (DWORD_PTR)&flags);
    if (mmr != MMSYSERR_NOERROR || device == (DWORD)-1) return WAVE_MAPPER;
    return (UINT)device;
}

// waveOut reports names in the system's ANSI code page; the rest of the
// program uses UTF-8.
static void ansi_to_utf8(const char* ansi, char* out, size_t cap) {
    wchar_t wide[64];
    if (MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide, sizeof wide / sizeof *wide) > 0 &&
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)cap, NULL, NULL) > 0)
        return;

    strncpy(out, ansi, cap - 1);        // no UTF-8 conversion on this system: keep the raw name
    out[cap - 1] = '\0';
}

void winmm_device_name(UINT device, char* out_utf8, size_t cap) {
    WAVEOUTCAPSA caps;
    if (waveOutGetDevCapsA(device, &caps, sizeof caps) == MMSYSERR_NOERROR)
        ansi_to_utf8(caps.szPname, out_utf8, cap);      // at most 31 characters: a WinMM limit
    else {
        strncpy(out_utf8, "default output device", cap - 1);
        out_utf8[cap - 1] = '\0';
    }
}

bool winmm_enumerate_devices(AT_DeviceEnumFn fn, void* user) {
    UINT count  = waveOutGetNumDevs();
    UINT preset = winmm_default_device();

    for (UINT i = 0; i < count; i++) {
        char name[128];
        winmm_device_name(i, name, sizeof name);
        fn(i, name, i == preset, user);
    }
    return true;
}
