/*
 * The settings screen in the terminal (`amped2_recomp --settings`, or
 * `scripts/run.sh --settings`): categories on the left, the selected
 * category's options on the right, a description of the highlighted option
 * below. Plain ANSI escapes and a raw terminal (termios / the Windows console
 * in VT mode), no curses, so the release binary needs nothing extra.
 *
 * Keys: Up/Down (or k/j, w/s) move, Left/Right (h/l, a/d) change a value or
 * switch between the panes, Tab switches panes, Enter starts the game with
 * the settings saved, S saves, Esc/Q leaves (asks when something is unsaved).
 */
#include "settings.h"

#if defined(AMPED2_HAVE_VULKAN)
#include <SDL.h>                /* the display list (the Vulkan build links SDL2) */
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

void amped2_settings_load_file_only(void);   /* settings.c */

/* ---- terminal --------------------------------------------------------- */

enum { K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_TAB, K_ENTER, K_ESC, K_SAVE, K_QUIT,
       K_OTHER };

#ifdef _WIN32
static DWORD s_old_in, s_old_out;
static UINT s_old_cp;

static void term_raw(void)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(in, &s_old_in);
    GetConsoleMode(out, &s_old_out);
    SetConsoleMode(out, s_old_out | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    s_old_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
}

static void term_restore(void)
{
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), s_old_in);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), s_old_out);
    SetConsoleOutputCP(s_old_cp);
}

static void term_size(int *w, int *h)
{
    CONSOLE_SCREEN_BUFFER_INFO ci;
    *w = 80; *h = 24;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci)) {
        *w = ci.srWindow.Right - ci.srWindow.Left + 1;
        *h = ci.srWindow.Bottom - ci.srWindow.Top + 1;
    }
}

static int read_key(void)
{
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        switch (_getch()) {
        case 72: return K_UP;
        case 80: return K_DOWN;
        case 75: return K_LEFT;
        case 77: return K_RIGHT;
        default: return K_OTHER;
        }
    }
    switch (c) {
    case 27:              return K_ESC;
    case '\t':            return K_TAB;
    case '\r': case '\n': return K_ENTER;
    case 'k': case 'w': case 'K': case 'W': return K_UP;
    case 'j': case 'J':   return K_DOWN;
    case 'h': case 'a': case 'H': case 'A': return K_LEFT;
    case 'l': case 'd': case 'L': case 'D': return K_RIGHT;
    case 's': case 'S':   return K_SAVE;
    case 'q': case 'Q':   return K_QUIT;
    default:              return K_OTHER;
    }
}
#else
static struct termios s_old;

static void term_raw(void)
{
    struct termios t;
    tcgetattr(0, &s_old);
    t = s_old;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
}

static void term_restore(void)
{
    tcsetattr(0, TCSANOW, &s_old);
}

static void term_size(int *w, int *h)
{
    struct winsize ws;
    *w = 80; *h = 24;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
        *w = ws.ws_col;
        *h = ws.ws_row;
    }
}

/* A byte, or -1 if none arrives within ms. */
static int read_byte(int ms)
{
    struct pollfd p = { 0, POLLIN, 0 };
    unsigned char c;
    if (poll(&p, 1, ms) <= 0 || read(0, &c, 1) != 1)
        return -1;
    return c;
}

static int read_key(void)
{
    int c = read_byte(-1);
    if (c == 27) {
        int c2 = read_byte(30), c3;
        if (c2 < 0)
            return K_ESC;                       /* a lone Esc */
        if (c2 != '[' && c2 != 'O')
            return K_OTHER;
        c3 = read_byte(30);
        switch (c3) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        default:
            while (c3 >= 0 && !(c3 >= 0x40 && c3 <= 0x7E))   /* rest of the sequence */
                c3 = read_byte(30);
            return K_OTHER;
        }
    }
    switch (c) {
    case '\t':            return K_TAB;
    case '\r': case '\n': return K_ENTER;
    case 'k': case 'w': case 'K': case 'W': return K_UP;
    case 'j': case 'J':   return K_DOWN;
    case 'h': case 'a': case 'H': case 'A': return K_LEFT;
    case 'l': case 'd': case 'L': case 'D': return K_RIGHT;
    case 's': case 'S':   return K_SAVE;
    case 'q': case 'Q':   return K_QUIT;
    case -1:              return K_QUIT;    /* stdin closed */
    default:              return K_OTHER;
    }
}
#endif

/* ---- output buffer ---------------------------------------------------- */

static char   s_out[1 << 16];
static size_t s_outn;

static void out(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(s_out + s_outn, sizeof s_out - s_outn, fmt, ap);
    va_end(ap);
    if (n > 0)
        s_outn += (size_t)n < sizeof s_out - s_outn ? (size_t)n : sizeof s_out - s_outn - 1;
}

static void flush_out(void)
{
    fwrite(s_out, 1, s_outn, stdout);
    fflush(stdout);
    s_outn = 0;
}

static void at(int row, int col) { out("\x1b[%d;%dH", row, col); }

/* Columns a UTF-8 string takes (every code point one column: the strings here
 * are Latin text, box drawing and arrows). */
static int cols(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

/* s, cut or padded to exactly w columns. */
static void field(const char *s, int w)
{
    int n = 0;
    const char *p = s;
    while (*p && n < w) {
        const char *q = p + 1;
        while (((unsigned char)*q & 0xC0) == 0x80)
            q++;
        out("%.*s", (int)(q - p), p);
        p = q;
        n++;
    }
    for (; n < w; n++)
        out(" ");
}

static void hline(const char *left, const char *mid, const char *right, int w, int split)
{
    int i;
    out("%s", left);
    for (i = 1; i < w - 1; i++)
        out("%s", (split && i == split) ? mid : "─");
    out("%s", right);
}

/* ---- the model -------------------------------------------------------- */

#define MAX_DISPLAYS 16
#define MAX_CHOICES  24

static struct {
    int  count;
    char name[MAX_DISPLAYS][96];
    int  refresh[MAX_DISPLAYS];
} s_disp;

static const int k_limits[] = { 0, 60, 75, 90, 120, 144, 165, 180, 240, 270, 360, -1 };
#define NLIMITS ((int)(sizeof k_limits / sizeof k_limits[0]))

typedef struct {
    const char *label;
    const char *help;
    int  (*count)(void);
    void (*text)(int i, char *buf, size_t n);
    int  (*get)(void);
    void (*set)(int i);
    int  (*enabled)(void);           /* NULL: always */
} Option;

typedef struct {
    const char   *title;
    const char   *blurb;
    const Option *opts;
    int           nopts;
} Category;

#define S g_amped2_settings

static int  n_scale(void)          { return AMPED2_SCALE_MAX; }
static int  g_scale(void)          { return S.resolution_scale - 1; }
static void s_scale(int i)         { S.resolution_scale = i + 1; }
static void t_scale(int i, char *b, size_t n)
{
    int h = 480 * (i + 1), w = (int)(h * amped2_aspect_value(S.aspect_ratio) + 0.5);
    snprintf(b, n, "%dx  (%d x %d)%s", i + 1, w, h, i == 0 ? "  original" : "");
}

static int  n_aspect(void)         { return AMPED2_ASPECT_COUNT; }
static int  g_aspect(void)         { return S.aspect_ratio; }
static void s_aspect(int i)        { S.aspect_ratio = i; }
static void t_aspect(int i, char *b, size_t n)
{
    static const char *const extra[AMPED2_ASPECT_COUNT] = {
        "original", "widescreen", "widescreen", "ultrawide" };
    snprintf(b, n, "%s  %s", amped2_aspect_name(i), extra[i]);
}

static int  n_fps(void)            { return AMPED2_FPS_COUNT; }
static int  g_fps(void)            { return S.framerate; }
static void s_fps(int i)           { S.framerate = i; }
static void t_fps(int i, char *b, size_t n)
{
    snprintf(b, n, "%s", i == AMPED2_FPS_ORIGINAL ? "Original  60 fps" : "Unlocked");
}

static int  n_limit(void)          { return NLIMITS; }
static int  g_limit(void)
{
    int i;
    for (i = 0; i < NLIMITS; i++)
        if (k_limits[i] == S.frame_limit)
            return i;
    return 0;
}
static void s_limit(int i)         { S.frame_limit = k_limits[i]; }
static void t_limit(int i, char *b, size_t n)
{
    int d = S.display < s_disp.count ? S.display : 0;
    if (k_limits[i] == 0) {
        if (s_disp.count && s_disp.refresh[d])
            snprintf(b, n, "Display refresh rate  (%d Hz)", s_disp.refresh[d]);
        else
            snprintf(b, n, "Display refresh rate");
    } else if (k_limits[i] < 0) {
        snprintf(b, n, "No limit");
    } else {
        snprintf(b, n, "%d fps", k_limits[i]);
    }
}
static int  e_limit(void)          { return S.framerate == AMPED2_FPS_UNLOCKED; }

static int  n_window(void)         { return AMPED2_WINDOW_COUNT; }
static int  g_window(void)         { return S.window_mode; }
static void s_window(int i)        { S.window_mode = i; }
static void t_window(int i, char *b, size_t n)
{
    static const char *const names[AMPED2_WINDOW_COUNT] = {
        "Windowed", "Fullscreen", "Borderless fullscreen" };
    snprintf(b, n, "%s", names[i]);
}

static int  n_display(void)        { return s_disp.count ? s_disp.count : 1; }
static int  g_display(void)        { return S.display < n_display() ? S.display : 0; }
static void s_display(int i)       { S.display = i; }
static void t_display(int i, char *b, size_t n)
{
    if (s_disp.count)
        snprintf(b, n, "%d: %s", i + 1, s_disp.name[i]);
    else
        snprintf(b, n, "%d: (no display information)", i + 1);
}
static int  e_display(void)        { return S.window_mode != AMPED2_WINDOWED; }

static const Option k_res_opts[] = {
    { "Internal resolution",
      "The resolution the game is drawn at before it is scaled to the window. "
      "1x is the Xbox's 640x480; every step multiplies both sides. Higher values "
      "sharpen edges and distant detail and cost GPU time (5x is 25 times the "
      "pixels of 1x per frame).",
      n_scale, t_scale, g_scale, s_scale, NULL },
};
static const Option k_aspect_opts[] = {
    { "Aspect ratio",
      "Rides get wider: more of the slope at the sides (Hor+), the height of "
      "the view stays as on the Xbox. The HUD keeps its 4:3 shape in the middle; "
      "menus and movies stay 4:3 with black bars. Objects at the far edges can "
      "pop in, as the game only draws what its original 4:3 view could see.",
      n_aspect, t_aspect, g_aspect, s_aspect, NULL },
};
static const Option k_fps_opts[] = {
    { "Frame rate",
      "Original paces the game to 60 frames per second like the Xbox. Unlocked "
      "draws as many frames as the limit below allows; the game runs on its own "
      "clock, so gameplay speed stays the same. How high it gets depends on the "
      "CPU: busy slopes are slower than the menus.",
      n_fps, t_fps, g_fps, s_fps, NULL },
    { "Frame limit",
      "The highest frame rate when unlocked. Display refresh rate follows the "
      "monitor the game runs on (detected at start) and gives the smoothest "
      "motion without tearing.",
      n_limit, t_limit, g_limit, s_limit, e_limit },
};
static const Option k_display_opts[] = {
    { "Window mode",
      "Windowed: a resizable window. Fullscreen: exclusive fullscreen at the "
      "monitor's current resolution. Borderless fullscreen: a window that covers "
      "the monitor, quick to Alt+Tab away from. Alt+Enter or F11 switch between "
      "windowed and the fullscreen mode in game.",
      n_window, t_window, g_window, s_window, NULL },
    { "Monitor",
      "Which monitor fullscreen and borderless fullscreen use.",
      n_display, t_display, g_display, s_display, e_display },
};

#undef S

static const Category k_cats[] = {
    { "Resolution Scaling", "Render the game above the Xbox's 640x480.",
      k_res_opts, (int)(sizeof k_res_opts / sizeof k_res_opts[0]) },
    { "Aspect Ratio", "Widescreen and ultrawide views.",
      k_aspect_opts, (int)(sizeof k_aspect_opts / sizeof k_aspect_opts[0]) },
    { "Framerate", "Beyond the Xbox's 60 frames per second.",
      k_fps_opts, (int)(sizeof k_fps_opts / sizeof k_fps_opts[0]) },
    { "Display", "Window mode and monitor.",
      k_display_opts, (int)(sizeof k_display_opts / sizeof k_display_opts[0]) },
};
#define NCATS ((int)(sizeof k_cats / sizeof k_cats[0]))

/* The actions under the categories. */
enum { ACT_START, ACT_SAVE, ACT_QUIT, NACTS };
static const char *const k_acts[NACTS] = { "Save and start the game", "Save", "Quit" };

static void query_displays(void)
{
#if defined(AMPED2_HAVE_VULKAN)
    int i, inited = SDL_WasInit(SDL_INIT_VIDEO) != 0;

    if (!inited && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return;
    s_disp.count = SDL_GetNumVideoDisplays();
    if (s_disp.count > MAX_DISPLAYS)
        s_disp.count = MAX_DISPLAYS;
    if (s_disp.count < 0)
        s_disp.count = 0;
    for (i = 0; i < s_disp.count; i++) {
        SDL_DisplayMode m;
        const char *nm = SDL_GetDisplayName(i);
        if (SDL_GetDesktopDisplayMode(i, &m) == 0) {
            s_disp.refresh[i] = m.refresh_rate;
            snprintf(s_disp.name[i], sizeof s_disp.name[i], "%.22s  %dx%d %d Hz",
                     nm ? nm : "Display", m.w, m.h, m.refresh_rate);
        } else {
            snprintf(s_disp.name[i], sizeof s_disp.name[i], "%s", nm ? nm : "Display");
        }
    }
    if (!inited)
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
#endif
}

/* ---- drawing ---------------------------------------------------------- */

/* Word-wrap text into lines of at most w columns; returns the line count. */
static int wrap(const char *text, int w, char lines[][256], int max)
{
    int n = 0;
    while (*text && n < max) {
        int len = (int)strlen(text), cut;
        while (*text == ' ')
            text++;
        len = (int)strlen(text);
        if (len <= w) {
            snprintf(lines[n++], 256, "%s", text);
            break;
        }
        for (cut = w; cut > 0 && text[cut] != ' '; cut--)
            ;
        if (cut == 0)
            cut = w;
        snprintf(lines[n++], 256, "%.*s", cut, text);
        text += cut;
    }
    return n;
}

typedef struct {
    int cat;        /* 0..NCATS-1 categories, NCATS.. actions */
    int opt;
    int pane;       /* 0 left, 1 right */
    int dirty;
    char status[160];
} Ui;

#define C_RESET  "\x1b[0m"
#define C_TITLE  "\x1b[1;38;5;231;48;5;25m"
#define C_FRAME  "\x1b[38;5;67m"
#define C_DIM    "\x1b[38;5;245m"
#define C_SEL    "\x1b[1;38;5;16;48;5;117m"
#define C_SELDIM "\x1b[38;5;16;48;5;110m"
#define C_HEAD   "\x1b[1;38;5;117m"
#define C_VALUE  "\x1b[1;38;5;222m"
#define C_KEY    "\x1b[1;38;5;117m"
#define C_WARN   "\x1b[1;38;5;214m"

static void draw(const Ui *u)
{
    int W, H, lw = 28, rw, i, row, top = 1;
    char buf[256];
    const Category *c = u->cat < NCATS ? &k_cats[u->cat] : NULL;

    term_size(&W, &H);
    if (W > 110) W = 110;
    if (W < 60 || H < 20) {
        out("\x1b[2J\x1b[H" "Please make the terminal at least 60x20 (now %dx%d).", W, H);
        flush_out();
        return;
    }
    rw = W - lw - 3;
    out("\x1b[H\x1b[2J");

    /* Title bar */
    at(top, 1);
    out(C_TITLE);
    snprintf(buf, sizeof buf, "  AMPED 2 RECOMP  ·  Graphics settings");
    field(buf, W);
    out(C_RESET);

    /* Frame */
    at(top + 1, 1);
    out(C_FRAME); hline("╭", "┬", "╮", W, lw + 1); out(C_RESET);
    for (row = top + 2; row < H - 3; row++) {
        at(row, 1);     out(C_FRAME "│" C_RESET);
        at(row, lw + 2); out(C_FRAME "│" C_RESET);
        at(row, W);     out(C_FRAME "│" C_RESET);
    }
    at(H - 3, 1);
    out(C_FRAME); hline("├", "┴", "┤", W, lw + 1); out(C_RESET);
    at(H - 2, 1); out(C_FRAME "│" C_RESET); at(H - 2, W); out(C_FRAME "│" C_RESET);
    at(H - 1, 1);
    out(C_FRAME); hline("╰", "─", "╯", W, 0); out(C_RESET);

    /* Left pane: categories, then the actions */
    row = top + 3;
    at(row - 1, 3); out(C_DIM "CATEGORIES" C_RESET);
    for (i = 0; i < NCATS + NACTS; i++) {
        int sel = i == u->cat;
        const char *name = i < NCATS ? k_cats[i].title : k_acts[i - NCATS];
        if (i == NCATS)
            row++;                          /* gap before the actions */
        at(row++, 2);
        out("%s", sel ? (u->pane == 0 ? C_SEL : C_SELDIM) : (i < NCATS ? "" : C_DIM));
        snprintf(buf, sizeof buf, " %s %s", sel ? "▸" : " ", name);
        field(buf, lw);
        out(C_RESET);
    }

    /* Right pane: the category's options */
    row = top + 2;
    if (c) {
        at(row++, lw + 4); out(C_HEAD "%s" C_RESET, c->title);
        at(row++, lw + 4); out(C_DIM); field(c->blurb, rw - 2); out(C_RESET);
        row++;
        for (i = 0; i < c->nopts; i++) {
            const Option *o = &c->opts[i];
            int sel = u->pane == 1 && i == u->opt, en = !o->enabled || o->enabled();
            int v = o->get(), lab = 22;
            char val[160];
            o->text(v, val, sizeof val);
            at(row, lw + 3);
            out("%s", sel ? C_SEL : (en ? "" : C_DIM));
            snprintf(buf, sizeof buf, " %s", o->label);
            field(buf, lab);
            out("%s", sel ? "" : (en ? C_VALUE : C_DIM));
            snprintf(buf, sizeof buf, "%s %s %s", v > 0 ? "◀" : " ", val,
                     v < o->count() - 1 ? "▶" : " ");
            field(buf, rw - lab);
            out(C_RESET);
            row++;
            if (!en) {
                at(row, lw + 3 + lab);
                out(C_DIM "  (%s)" C_RESET,
                    o->enabled == e_limit ? "only when the frame rate is unlocked"
                                          : "only for fullscreen / borderless");
                row++;
            }
            row++;
        }
        /* Help for the highlighted option */
        if (u->pane == 1 || c->nopts == 1) {
            char lines[12][256];
            int n = wrap(c->opts[u->pane == 1 ? u->opt : 0].help, rw - 3, lines, 12), k;
            row++;
            for (k = 0; k < n && row < H - 4; k++) {
                at(row++, lw + 4);
                out(C_DIM "%s" C_RESET, lines[k]);
            }
        }
    } else {
        const char *what = u->cat == NCATS + ACT_START
                ? "Saves the settings and starts the game with them."
                : u->cat == NCATS + ACT_SAVE ? "Saves the settings and stays here."
                : "Leaves without starting the game.";
        at(row++, lw + 4); out(C_HEAD "%s" C_RESET, k_acts[u->cat - NCATS]);
        at(row + 1, lw + 4); out(C_DIM "%s" C_RESET, what);
        at(row + 3, lw + 4); out(C_DIM "File: " C_RESET); field(amped2_settings_path(), rw - 8);
    }

    /* Status / key help */
    at(H - 2, 3);
    if (u->status[0]) {
        out(C_WARN); field(u->status, W - 4); out(C_RESET);
    } else {
        out(C_KEY "↑↓" C_RESET " move  " C_KEY "←→" C_RESET " change  "
            C_KEY "Tab" C_RESET " pane  " C_KEY "Enter" C_RESET " save & start  "
            C_KEY "S" C_RESET " save  " C_KEY "Q" C_RESET " quit%s",
            u->dirty ? C_WARN "   ● unsaved changes" C_RESET : "");
    }
    at(H, 1);
    flush_out();
}

/* ---- main loop -------------------------------------------------------- */

static int option_enabled(const Option *o) { return !o->enabled || o->enabled(); }

static void change(Ui *u, int dir)
{
    const Option *o;
    int v;

    if (u->cat >= NCATS)
        return;
    o = &k_cats[u->cat].opts[u->opt];
    if (!option_enabled(o))
        return;
    v = o->get() + dir;
    if (v < 0 || v >= o->count())
        return;
    o->set(v);
    u->dirty = 1;
}

static int save(Ui *u)
{
    if (amped2_settings_save() != 0) {
        snprintf(u->status, sizeof u->status, "Could not write %s", amped2_settings_path());
        return 0;
    }
    u->dirty = 0;
    snprintf(u->status, sizeof u->status, "Saved to %s", amped2_settings_path());
    return 1;
}

int amped2_settings_tui(void)
{
    Ui u;
    int result = -1, confirm_quit = 0;

    memset(&u, 0, sizeof u);
    amped2_settings_load_file_only();
    query_displays();
    if (g_amped2_settings.display >= s_disp.count && s_disp.count)
        g_amped2_settings.display = 0;

    term_raw();
    out("\x1b[?1049h\x1b[?25l");                /* alternate screen, hide cursor */
    while (result < 0) {
        int k;
        draw(&u);
        k = read_key();
        if (k != K_OTHER && k != K_NONE)
            u.status[0] = 0;
        if (confirm_quit) {
            confirm_quit = 0;
            if (k == K_QUIT || k == K_ESC || k == K_ENTER)
                result = 0;
            else if (k == K_SAVE && save(&u))
                result = 0;
            continue;
        }
        switch (k) {
        case K_UP:
            if (u.pane == 0) {
                if (u.cat > 0) { u.cat--; u.opt = 0; }
            } else if (u.opt > 0) {
                u.opt--;
            }
            break;
        case K_DOWN:
            if (u.pane == 0) {
                if (u.cat < NCATS + NACTS - 1) { u.cat++; u.opt = 0; }
            } else if (u.opt < k_cats[u.cat].nopts - 1) {
                u.opt++;
            }
            break;
        case K_LEFT:
            if (u.pane == 1) {
                const Option *o = &k_cats[u.cat].opts[u.opt];
                if (o->get() == 0 || !option_enabled(o))
                    u.pane = 0;                 /* at the left end: back to the list */
                else
                    change(&u, -1);
            }
            break;
        case K_RIGHT:
            if (u.pane == 0) {
                if (u.cat < NCATS) u.pane = 1;
            } else {
                change(&u, +1);
            }
            break;
        case K_TAB:
            if (u.cat < NCATS)
                u.pane ^= 1;
            break;
        case K_ENTER:
            if (u.cat < NCATS) {
                if (u.pane == 0) { u.pane = 1; break; }
                if (save(&u)) result = 1;   /* Enter in the options: save & start */
            } else if (u.cat == NCATS + ACT_START) {
                if (save(&u)) result = 1;
            } else if (u.cat == NCATS + ACT_SAVE) {
                save(&u);
            } else {
                if (u.dirty) {
                    snprintf(u.status, sizeof u.status,
                             "Unsaved changes – Q/Esc/Enter: quit without saving, S: save and quit, other key: stay");
                    confirm_quit = 1;
                } else {
                    result = 0;
                }
            }
            break;
        case K_SAVE:
            save(&u);
            break;
        case K_ESC:
            if (u.pane == 1) { u.pane = 0; break; }
            /* fall through */
        case K_QUIT:
            if (u.dirty) {
                snprintf(u.status, sizeof u.status,
                         "Unsaved changes – Q/Esc/Enter: quit without saving, S: save and quit, other key: stay");
                confirm_quit = 1;
            } else {
                result = 0;
            }
            break;
        default:
            break;
        }
    }
    out("\x1b[0m\x1b[?25h\x1b[?1049l");         /* back to the normal screen */
    flush_out();
    term_restore();
    if (result == 1)
        amped2_settings_load();                  /* with this run's overrides on top */
    return result == 1;
}
