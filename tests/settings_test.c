/*
 * tests/settings_test.c -- src/settings/settings.c: defaults, parsing,
 * environment overrides and the save/load round trip.
 *
 * Runs in its working directory: the settings file goes to
 * settings_test_out/sub/settings.ini (AMPED2_SETTINGS), so saving also has to
 * create the missing directories. Built and run by ctest (tests/CMakeLists.txt).
 */
#include "settings/settings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void amped2_settings_load_file_only(void);   /* settings.c */

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define PATH "settings_test_out/sub/settings.ini"

static const char *const k_env[] = {
    "AMPED2_RESOLUTION_SCALE", "AMPED2_ASPECT_RATIO", "AMPED2_FRAMERATE",
    "AMPED2_FRAME_LIMIT", "AMPED2_WINDOW_MODE", "AMPED2_DISPLAY" };

/* An empty value counts as unset (settings.c skips it), so this also clears. */
static void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

static void clear_overrides(void)
{
    size_t i;
    for (i = 0; i < sizeof k_env / sizeof k_env[0]; i++)
        set_env(k_env[i], "");
}

static void write_file(const char *text)
{
    FILE *f = fopen(PATH, "w");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static int is_defaults(const Amped2Settings *s)
{
    return s->resolution_scale == 1 && s->aspect_ratio == AMPED2_ASPECT_4_3 &&
           s->framerate == AMPED2_FPS_ORIGINAL && s->frame_limit == 0 &&
           s->window_mode == AMPED2_WINDOWED && s->display == 0;
}

static void test_path(void)
{
    CHECK(!strcmp(amped2_settings_path(), PATH));
}

static void test_missing_file(void)
{
    remove(PATH);
    g_amped2_settings.resolution_scale = 4;      /* must be reset */
    amped2_settings_load();
    CHECK(is_defaults(&g_amped2_settings));
}

static void test_save_creates_dirs(void)
{
    remove(PATH);
    amped2_settings_load();
    CHECK(amped2_settings_save() == 0);
    {
        FILE *f = fopen(PATH, "r");
        CHECK(f != NULL);
        if (f)
            fclose(f);
    }
}

static void test_parse(void)
{
    const Amped2Settings *s = &g_amped2_settings;

    write_file("# comment\n"
               "; also a comment\n"
               "[graphics]\n"
               "  resolution_scale   =  3  \n"
               "aspect_ratio=16:9\n"
               "framerate = unlocked\n"
               "frame_limit = 144\n"
               "unknown_key = whatever\n"
               "no equals sign here\n"
               "\n"
               "[display]\n"
               "window_mode = borderless\n"
               "display = 2\n");
    amped2_settings_load();
    CHECK(s->resolution_scale == 3);
    CHECK(s->aspect_ratio == AMPED2_ASPECT_16_9);
    CHECK(s->framerate == AMPED2_FPS_UNLOCKED);
    CHECK(s->frame_limit == 144);
    CHECK(s->window_mode == AMPED2_BORDERLESS);
    CHECK(s->display == 2);

    write_file("frame_limit = none\n");
    amped2_settings_load();
    CHECK(s->frame_limit == -1);
    write_file("frame_limit = display\n");
    amped2_settings_load();
    CHECK(s->frame_limit == 0);
}

/* Out-of-range or unknown values keep the default. */
static void test_invalid_values(void)
{
    write_file("resolution_scale = 0\n"
               "aspect_ratio = 5:4\n"
               "framerate = fast\n"
               "frame_limit = 10\n"
               "window_mode = maximised\n"
               "display = -1\n");
    amped2_settings_load();
    CHECK(is_defaults(&g_amped2_settings));

    write_file("resolution_scale = 6\nframe_limit = 1001\ndisplay = 64\n");
    amped2_settings_load();
    CHECK(is_defaults(&g_amped2_settings));
}

static void test_env_overrides(void)
{
    const Amped2Settings *s = &g_amped2_settings;

    write_file("resolution_scale = 2\naspect_ratio = 16:10\n");
    set_env("AMPED2_RESOLUTION_SCALE", "5");
    set_env("AMPED2_WINDOW_MODE", "fullscreen");
    set_env("AMPED2_FRAME_LIMIT", "none");
    set_env("AMPED2_DISPLAY", "99");             /* invalid: ignored */
    amped2_settings_load();
    CHECK(s->resolution_scale == 5);
    CHECK(s->aspect_ratio == AMPED2_ASPECT_16_10); /* from the file */
    CHECK(s->window_mode == AMPED2_FULLSCREEN);
    CHECK(s->frame_limit == -1);
    CHECK(s->display == 0);

    /* The TUI sees the file without this run's overrides. */
    amped2_settings_load_file_only();
    CHECK(s->resolution_scale == 2);
    CHECK(s->window_mode == AMPED2_WINDOWED);
    clear_overrides();
}

static void round_trip(int scale, int aspect, int fps, int limit, int window, int display)
{
    Amped2Settings *s = &g_amped2_settings;

    s->resolution_scale = scale;
    s->aspect_ratio = aspect;
    s->framerate = fps;
    s->frame_limit = limit;
    s->window_mode = window;
    s->display = display;
    CHECK(amped2_settings_save() == 0);
    memset(s, 0x55, sizeof *s);
    amped2_settings_load();
    CHECK(s->resolution_scale == scale);
    CHECK(s->aspect_ratio == aspect);
    CHECK(s->framerate == fps);
    CHECK(s->frame_limit == limit);
    CHECK(s->window_mode == window);
    CHECK(s->display == display);
}

static void test_round_trip(void)
{
    round_trip(1, AMPED2_ASPECT_4_3, AMPED2_FPS_ORIGINAL, 0, AMPED2_WINDOWED, 0);
    round_trip(AMPED2_SCALE_MAX, AMPED2_ASPECT_21_9, AMPED2_FPS_UNLOCKED, -1, AMPED2_BORDERLESS, 3);
    round_trip(2, AMPED2_ASPECT_16_10, AMPED2_FPS_UNLOCKED, 240, AMPED2_FULLSCREEN, 1);
}

static void test_aspect(void)
{
    CHECK(fabs(amped2_aspect_value(AMPED2_ASPECT_16_9) - 16.0 / 9.0) < 1e-12);
    CHECK(fabs(amped2_aspect_value(AMPED2_ASPECT_21_9) - 21.0 / 9.0) < 1e-12);
    CHECK(fabs(amped2_aspect_value(-1) - 4.0 / 3.0) < 1e-12);
    CHECK(fabs(amped2_aspect_value(AMPED2_ASPECT_COUNT) - 4.0 / 3.0) < 1e-12);
    CHECK(!strcmp(amped2_aspect_name(AMPED2_ASPECT_16_10), "16:10"));
    CHECK(!strcmp(amped2_aspect_name(42), "4:3"));
}

int main(void)
{
    set_env("AMPED2_SETTINGS", PATH);
    clear_overrides();

    test_path();
    test_missing_file();
    test_save_creates_dirs();
    test_parse();
    test_invalid_values();
    test_env_overrides();
    test_round_trip();
    test_aspect();

    if (fails) {
        fprintf(stderr, "settings_test: %d check(s) failed\n", fails);
        return 1;
    }
    printf("settings_test: all checks passed\n");
    return 0;
}
