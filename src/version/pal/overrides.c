/**
 * Hand-written replacements for the PAL (Europe) default.xbe.
 *
 * Every `void sub_XXXXXXXX(void)` defined here replaces the generated body:
 * run_pipeline.sh passes this file to the recompiler with --exclude-manual.
 * An `extern void sub_X_gen(void);` keeps the generated body under that name
 * for a wrapper. The bodies are in src/recomp_manual.c (shared by every
 * build); see the comments there for what each one does and why.
 */
#include <stdint.h>

extern void amped2_media_check(void);
extern void amped2_fence_wait(void (*kickoff)(void), uint32_t ret_va);
extern void amped2_boot_movie(const char *which, const char *skip_env, void (*play)(void));
extern void amped2_xmv_fetch(void (*fetch)(void));
extern void amped2_xnet_get_title_xnaddr(void);

/* XAPI media check, called once from 0x002553DC; certificate SHA-1 check at 0x0025514B. */
void sub_00255202(void) { amped2_media_check(); }

/* D3D_BlockOnTime; FUN_0031E5A0(0) kicks off the pushbuffer, the original returns to 0x0031E80D. */
extern void sub_0031E5A0(void);
void sub_0031E7E0(void) { amped2_fence_wait(sub_0031E5A0, 0x0031E80Du); }

/* Boot movies from FUN_00210E00 (the boot state's update, vtable 0x0036BFC4). */
extern void sub_00211030_gen(void);
extern void sub_002110E0_gen(void);
extern void sub_00210F60_gen(void);
void sub_00211030(void) { amped2_boot_movie("MGS logo", "AMPED2_SKIP_LOGOS", sub_00211030_gen); }
void sub_002110E0(void) { amped2_boot_movie("XSN logo", "AMPED2_SKIP_LOGOS", sub_002110E0_gen); }
void sub_00210F60(void) { amped2_boot_movie("intro", "AMPED2_SKIP_INTRO", sub_00210F60_gen); }

/* XMV frame fetch (XMV section). */
extern void sub_002CDE60_gen(void);
void sub_002CDE60(void) { amped2_xmv_fetch(sub_002CDE60_gen); }

/* XNetGetTitleXnAddr: `mov ecx, [0x0038C238]; jmp 0x002BB570`. */
void sub_002BB66E(void) { amped2_xnet_get_title_xnaddr(); }
