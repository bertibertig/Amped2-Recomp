/* Amped 2 PAL (Europe) default.xbe, SHA-1 38e63cd016bf084d999e0a911933a52191bfc378: data addresses used by
 * src/main.c and src/recomp_manual.c. Function addresses are in overrides.c next to this file. */
#ifndef AMPED2_ADDRS_H
#define AMPED2_ADDRS_H

#define AMPED2_XBE_VERSION      "PAL"
#define AMPED2_XBE_TIMESTAMP    0x3F7D0DFDu  /* header +0x114: built 2003-10-03 05:49:49 UTC (docs/notes.md) */
#define AMPED2_NATIVE_HD        0            /* the game's 720p widescreen by default: camera update (aspect hook) not located for PAL yet */
#define AMPED2_ENTRY_POINT      0x00254899u  /* XBE entry point VA */
#define AMPED2_D3D_DEVICE_PTR   0x0032CEF8u  /* D3D global device pointer (put/limit at +0x00/+0x04) */
#define AMPED2_PB_RECORDS       0x0032ACC0u  /* RunPushBuffer patch records, 20 bytes each */
#define AMPED2_PB_PENDING       0x0032AAACu  /* count of pending RunPushBuffer patches */

#endif
