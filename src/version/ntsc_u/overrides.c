/**
 * Hand-written replacements for the NTSC-U (North America) default.xbe.
 *
 * The same set as src/version/pal/overrides.c, at this build's addresses
 * (PAL address in each comment). Every `void sub_XXXXXXXX(void)` defined here
 * replaces the generated body: run_pipeline.sh passes this file to the
 * recompiler with --exclude-manual. An `extern void sub_X_gen(void);` keeps
 * the generated body under that name for a wrapper. The bodies are in
 * src/recomp_manual.c (shared by every build).
 */
#include <stdint.h>

extern void amped2_media_check(void);
extern void amped2_fence_wait(void (*kickoff)(void), uint32_t ret_va);
extern void amped2_boot_movie(const char *which, const char *skip_env, void (*play)(void));
extern void amped2_xmv_fetch(void (*fetch)(void));
extern void amped2_xnet_get_title_xnaddr(void);

/* XAPI media check (PAL 0x00255202): reads the certificate dword at +0x1D8. */
void sub_00254EF2(void) { amped2_media_check(); }

/* D3D_BlockOnTime (PAL 0x0031E7E0); kick-off FUN_0031E2A0(0), returning to 0x0031E50D. */
extern void sub_0031E2A0(void);
void sub_0031E4E0(void) { amped2_fence_wait(sub_0031E2A0, 0x0031E50Du); }

/* Boot movies (PAL 0x00211030 / 0x002110E0 / 0x00210F60), called in that order from the boot state's update,
 * which tests this+0x3D at 0x00210B03; flag global 0x003819B8, intro paths at 0x003819B0/0x003819B4. */
extern void sub_00210CD0_gen(void);
extern void sub_00210D80_gen(void);
extern void sub_00210C00_gen(void);
void sub_00210CD0(void) { amped2_boot_movie("MGS logo", "AMPED2_SKIP_LOGOS", sub_00210CD0_gen); }
void sub_00210D80(void) { amped2_boot_movie("XSN logo", "AMPED2_SKIP_LOGOS", sub_00210D80_gen); }
void sub_00210C00(void) { amped2_boot_movie("intro", "AMPED2_SKIP_INTRO", sub_00210C00_gen); }

/* XMV frame fetch (PAL 0x002CDE60). */
extern void sub_002CDB60_gen(void);
void sub_002CDB60(void) { amped2_xmv_fetch(sub_002CDB60_gen); }

/* XNetGetTitleXnAddr (PAL 0x002BB66E): `mov ecx, [0x0038BF38]; jmp 0x002BB270`. */
void sub_002BB36E(void) { amped2_xnet_get_title_xnaddr(); }
