/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>   /* ptrdiff_t */
#include <stdlib.h>   /* getenv */
#include <time.h>     /* timespec_get */

/* The runtime's register model and ICALL declarations. Use these rather than
 * spelling the externs out: g_eax, g_esp ... are thread-local (RECOMP_TLS),
 * and a plain `extern uint32_t g_eax` silently refers to the wrong storage. */
#include "recomp/gen/recomp_types.h"   /* copied there by the recompiler */

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Do not define them here as well -- a duplicate symbol.
 */

/* ── Hand-written function bodies ──────────────────────────
 *
 * Every `void sub_XXXXXXXX(void)` *defined* in this file replaces the
 * generated body: run_pipeline.sh passes this file to the recompiler with
 * --exclude-manual, which skips generating those functions. So direct calls
 * reach these too, not only indirect ones (recomp_lookup_manual below).
 * After adding or removing one, re-run `scripts/run_pipeline.sh recomp`.
 */

/* XAPI media check (0x00255202, called once from 0x002553DC during process
 * start-up).
 *
 * Amped 2's certificate has bit 0 of the dword at +0x1D8 set, so besides the
 * MODE SENSE authentication page (which the runtime answers) XAPI reads the
 * disc's security sectors with READ DVD STRUCTURE (0xAD, 0x664 bytes) and
 * checks a SHA-1 over them against the certificate (0x0025514B). There is no
 * drive and no security sector here; the check fails five times and XAPI
 * leaves through XLaunchNewImage(NULL) -> HalReturnToFirmware(2), i.e. back
 * to the dashboard, before the title's main() runs.
 *
 * Returning STATUS_SUCCESS is what a genuine disc yields. cdecl, no
 * arguments, plain `ret`. */
void sub_00255202(void)
{
    g_eax = 0;      /* STATUS_SUCCESS */
    g_esp += 4;     /* ret */
}

/* D3D's fence wait (D3D_BlockOnTime, 0x0031E7E0): the fence in eax, a flags
 * word on the stack, `ret 4`. Replaced as xboxrecomp's
 * docs/technical/d3d8ltcg-device-context.md describes for titles whose
 * pushbuffer the executor runs (RECOMP_PB_EXEC).
 *
 * Before, main.c mirrored the fence ("the GPU is always caught up"). With the
 * executor running behind the title, that lets D3D free and reuse vertex
 * memory and ring space the executor has not walked yet; the toolkit docs
 * list the symptoms (data drawn as state, objects drawn black). Some menu
 * batches did read freed blocks (0xFDFDFDFD, the game allocator's free fill,
 * FUN_000DDC90). Note: the riders' missing upper body is NOT fixed by this --
 * those batches are never submitted at all (docs/notes.md).
 *
 * Now the executor writes the real semaphore (device + 0x30) at every
 * BACK_END_WRITE_SEMAPHORE_RELEASE, and this waits for it like the original's
 * spin at 0x0031E901, bounded at 250 ms. Like the original, the newest fence
 * first kicks off the pushbuffer holding its release (FUN_0031E5A0(0)); the
 * original's other branch waits for a GPU interrupt the runtime never raises,
 * which is why the whole function is replaced rather than wrapped.
 * Device pointer: 0x0032CEF8 (see src/main.c, docs/notes.md). */
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
extern void sub_0031E5A0(void);
static double now_sec(void);
static void host_yield(void);

#define AMPED2_D3D_DEVICE_PTR 0x0032CEF8u

static int fence_passed(uint32_t dev, uint32_t sem, uint32_t fence)
{
    uint32_t submitted = MEM32(dev + 0x2C);
    /* As the original compares: unsigned distances from the newest fence. */
    return submitted - fence >= submitted - MEM32(sem);
}

void sub_0031E7E0(void)
{
    static uint32_t targeted;
    uint32_t fence = g_eax;
    uint32_t dev = MEM32(AMPED2_D3D_DEVICE_PTR);
    uint32_t sem;
    double t0;

    if (!dev || !(sem = MEM32(dev + 0x30))) {
        g_esp += 8;                         /* ret 4 */
        return;
    }
    /* Re-applied on every call: D3D rewrites the field on a device reset. */
    if (sem != targeted) {
        nv2a_pb_set_semaphore_target(sem);
        fprintf(stderr, "[D3D] GPU semaphore at 0x%08X (fence wait replaced)\n", sem);
        targeted = sem;
    }
    if (!fence_passed(dev, sem, fence)) {
        if (fence == MEM32(dev + 0x2C)) {
            PUSH32(g_esp, 0);               /* FUN_0031E5A0(0): kick off */
            PUSH32(g_esp, 0x0031E80Du);
            sub_0031E5A0();
        }
        t0 = now_sec();
        while (!fence_passed(dev, sem, fence)) {
            if (now_sec() - t0 > 0.25) {
                static int warned;
                if (warned++ < 5)
                    fprintf(stderr, "[D3D] fence 0x%08X not reached in 250 ms "
                                    "(semaphore 0x%08X)\n", fence, MEM32(sem));
                break;
            }
            host_yield();
        }
    }
    g_esp += 8;                             /* ret 4 */
}

/* XNetGetTitleXnAddr (exported stub 0x002BB66E: `mov ecx, [0x0038C238]; jmp
 * 0x002BB570`), DWORD WINAPI XNetGetTitleXnAddr(XNADDR *pxna).
 *
 * There is no network adapter here, so XNet's start-up never completes and
 * the call answers XNET_GET_XNADDR_PENDING (0) forever. The game's network
 * thread waits for it right after XNetStartup (`while (XNetGetTitleXnAddr(&x)
 * == PENDING);` in 0x001BE680, under the global lock 0x0038C890) and spun
 * there for good, which in turn stalled the main thread before the first
 * movie got past one frame.
 *
 * Answer what a console without a cable reports: XNET_GET_XNADDR_NONE (1),
 * with the XNADDR zeroed (36 bytes, as the original clears it). That is also
 * what the original returns itself when XNet was never started (ecx == 0 at
 * 0x002BB570). stdcall, one argument: ret 4. Xbox Live is gone anyway; the
 * game should treat the port as an offline console. */
/* Boot movies.
 *
 * FUN_00210E00 (the boot state's update, vtable 0x0036BFC4) plays three
 * movies in a row while its "play movies" flag (this+0x3D, from the global
 * 0x00381CB8) is set: MGS (0x00211030, d:\Movies\MGS.xmv), XSN Sports
 * (0x002110E0, d:\Movies\XSN.xmv -- not on the PAL disc) and the intro
 * (0x00210F60, Z:\ or d:\Movies\Intro.xmv). Each builds the path and runs the
 * blocking player FUN_00150EA0; all are cdecl, no arguments, plain `ret`.
 *
 *   AMPED2_SKIP_LOGOS=1  no MGS / XSN logo movies (default in scripts/run.sh)
 *   AMPED2_SKIP_INTRO=1  no intro movie either
 *
 * The wrappers keep the generated bodies under the _gen name (the recompiler
 * emits sub_X_gen when this file defines sub_X and calls sub_X_gen). */
extern void sub_00211030_gen(void);
extern void sub_002110E0_gen(void);
extern void sub_00210F60_gen(void);

static int env_flag(const char *name)
{
    const char *v = getenv(name);
    return v && *v == '1';
}

static void skip_movie(const char *which)
{
    static int said;
    if (said++ < 3)
        fprintf(stderr, "[BOOT] skipping the %s movie\n", which);
    g_eax = 0;
    g_esp += 4;     /* ret */
}

void sub_00211030(void)             /* MGS logo */
{
    if (env_flag("AMPED2_SKIP_LOGOS")) { skip_movie("MGS logo"); return; }
    sub_00211030_gen();
}

void sub_002110E0(void)             /* XSN Sports logo */
{
    if (env_flag("AMPED2_SKIP_LOGOS")) { skip_movie("XSN logo"); return; }
    sub_002110E0_gen();
}

void sub_00210F60(void)             /* intro */
{
    if (env_flag("AMPED2_SKIP_INTRO")) { skip_movie("intro"); return; }
    sub_00210F60_gen();
}

/* XMV frame fetch (XMV section 0x002CDE60, stdcall, 4 arguments; reached
 * from the game's movie loop through the adapter FUN_00150E80, once per loop
 * iteration). AMPED2_XMV_STATS=1 prints every 5 s how often it was called,
 * how long the calls took and what they returned -- the question being
 * whether a movie lags because the loop asks too rarely or because the
 * decode itself is too slow. */
extern void sub_002CDE60_gen(void);

static void host_yield(void)
{
#ifdef _WIN32
    extern __declspec(dllimport) void __stdcall Sleep(unsigned long);
    Sleep(0);
#else
    extern int sched_yield(void);
    sched_yield();
#endif
}

static double now_sec(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

void sub_002CDE60(void)
{
    static int on = -1;
    static double t_start, t_busy, t_max;
    static unsigned calls, results[4];

    if (on < 0)
        on = env_flag("AMPED2_XMV_STATS");
    if (!on) {
        sub_002CDE60_gen();
        return;
    }
    {
        double t0 = now_sec(), dt;
        if (!t_start)
            t_start = t0;
        sub_002CDE60_gen();
        dt = now_sec() - t0;
        t_busy += dt;
        if (dt > t_max)
            t_max = dt;
        calls++;
        results[g_eax == 0 ? 0 : (int32_t)g_eax < 0 ? 1 : g_eax < 16 ? 2 : 3]++;
        if (t0 + dt - t_start >= 5.0) {
            fprintf(stderr, "[XMV] %u calls/5s, busy %.0f ms (max %.1f ms/call), "
                            "eax: 0 x%u, <0 x%u, small x%u, other x%u\n",
                    calls, t_busy * 1e3, t_max * 1e3,
                    results[0], results[1], results[2], results[3]);
            t_start = t0 + dt;
            t_busy = t_max = 0;
            calls = results[0] = results[1] = results[2] = results[3] = 0;
        }
    }
}

void sub_002BB66E(void)
{
    uint32_t pxna = MEM32(g_esp + 4);
    uint32_t i;

    if (pxna)
        for (i = 0; i < 36; i++)
            MEM8(pxna + i) = 0;
    g_eax = 1;      /* XNET_GET_XNADDR_NONE */
    g_esp += 8;     /* ret 4 */
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}
