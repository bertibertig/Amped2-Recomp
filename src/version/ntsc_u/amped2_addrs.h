/* Amped 2 NTSC-U (North America) default.xbe, SHA-1 5f432b86775b7b334212a3bc09d2063f670865b7: data addresses
 * used by src/main.c and src/recomp_manual.c. Function addresses are in overrides.c next to this file.
 *
 * Found from the PAL addresses (src/version/pal/) by disassembly, see docs/notes.md: the XDK library sections,
 * .rdata and .data sit 0x300 lower than on PAL, the XAPI/CRT code at the end of .text 0x310 lower, game code
 * by varying amounts. */
#ifndef AMPED2_ADDRS_H
#define AMPED2_ADDRS_H

#define AMPED2_XBE_VERSION      "NTSC-U"
#define AMPED2_ENTRY_POINT      0x00254589u  /* XBE entry point VA (debug-key encoded in this dump's header) */
#define AMPED2_D3D_DEVICE_PTR   0x0032CBF8u  /* D3D global device pointer: `mov esi, [0x32CBF8]` in 0x0031E4E0 */
#define AMPED2_PB_RECORDS       0x0032A9C0u  /* `lea eax, [eax*4 + 0x32A9C0]` (20-byte records) at 0x00322666 */
#define AMPED2_PB_PENDING       0x0032A7ACu  /* `dec dword ptr [0x32A7AC]` at 0x00322683 */

#endif
