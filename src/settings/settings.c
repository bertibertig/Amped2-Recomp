/*
 * Graphics settings file (settings.h). Plain "key = value" lines under a
 * [graphics] section; unknown keys and comments are ignored, so the file can
 * grow with the enhancement list (docs/TODO.md) without breaking old ones.
 */
#include "settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

Amped2Settings g_amped2_settings;

static char s_path[1024];

static const char *const k_aspect_names[AMPED2_ASPECT_COUNT] = { "4:3", "16:9", "16:10", "21:9" };
static const double k_aspect_values[AMPED2_ASPECT_COUNT] = {
    4.0 / 3.0, 16.0 / 9.0, 16.0 / 10.0, 21.0 / 9.0 };
static const char *const k_fps_names[AMPED2_FPS_COUNT] = { "original", "unlocked" };
static const char *const k_window_names[AMPED2_WINDOW_COUNT] = {
    "windowed", "fullscreen", "borderless" };

double amped2_aspect_value(int aspect)
{
    return (aspect >= 0 && aspect < AMPED2_ASPECT_COUNT) ? k_aspect_values[aspect]
                                                        : k_aspect_values[0];
}

const char *amped2_aspect_name(int aspect)
{
    return (aspect >= 0 && aspect < AMPED2_ASPECT_COUNT) ? k_aspect_names[aspect]
                                                        : k_aspect_names[0];
}

static void defaults(Amped2Settings *s)
{
    s->resolution_scale = 1;
    s->aspect_ratio = AMPED2_ASPECT_4_3;
    s->framerate = AMPED2_FPS_ORIGINAL;
    s->frame_limit = 0;
    s->window_mode = AMPED2_WINDOWED;
    s->display = 0;
}

static int name_index(const char *v, const char *const *names, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(v, names[i]))
            return i;
    return -1;
}

/* One key; values out of range keep what was there. */
static void apply(Amped2Settings *s, const char *key, const char *v)
{
    int i;

    if (!strcmp(key, "resolution_scale")) {
        i = atoi(v);
        if (i >= 1 && i <= AMPED2_SCALE_MAX)
            s->resolution_scale = i;
    } else if (!strcmp(key, "aspect_ratio")) {
        if ((i = name_index(v, k_aspect_names, AMPED2_ASPECT_COUNT)) >= 0)
            s->aspect_ratio = i;
    } else if (!strcmp(key, "framerate")) {
        if ((i = name_index(v, k_fps_names, AMPED2_FPS_COUNT)) >= 0)
            s->framerate = i;
    } else if (!strcmp(key, "frame_limit")) {
        if (!strcmp(v, "display"))
            s->frame_limit = 0;
        else if (!strcmp(v, "none"))
            s->frame_limit = -1;
        else if ((i = atoi(v)) >= 30 && i <= 1000)
            s->frame_limit = i;
    } else if (!strcmp(key, "window_mode")) {
        if ((i = name_index(v, k_window_names, AMPED2_WINDOW_COUNT)) >= 0)
            s->window_mode = i;
    } else if (!strcmp(key, "display")) {
        i = atoi(v);
        if (i >= 0 && i < 64)
            s->display = i;
    }
}

const char *amped2_settings_path(void)
{
    const char *e;

    if (s_path[0])
        return s_path;
    if ((e = getenv("AMPED2_SETTINGS")) && *e) {
        snprintf(s_path, sizeof s_path, "%s", e);
        return s_path;
    }
#ifdef _WIN32
    if ((e = getenv("APPDATA")) && *e)
        snprintf(s_path, sizeof s_path, "%s\\amped2-recomp\\settings.ini", e);
    else
        snprintf(s_path, sizeof s_path, "settings.ini");
#else
    if ((e = getenv("XDG_CONFIG_HOME")) && *e)
        snprintf(s_path, sizeof s_path, "%s/amped2-recomp/settings.ini", e);
    else if ((e = getenv("HOME")) && *e)
        snprintf(s_path, sizeof s_path, "%s/.config/amped2-recomp/settings.ini", e);
    else
        snprintf(s_path, sizeof s_path, "settings.ini");
#endif
    return s_path;
}

static char *trim(char *p)
{
    char *end;
    while (isspace((unsigned char)*p))
        p++;
    end = p + strlen(p);
    while (end > p && isspace((unsigned char)end[-1]))
        *--end = 0;
    return p;
}

static void load_file(Amped2Settings *s)
{
    FILE *f = fopen(amped2_settings_path(), "r");
    char line[256];

    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        char *p = trim(line), *eq;
        if (!*p || *p == '#' || *p == ';' || *p == '[')
            continue;
        if (!(eq = strchr(p, '=')))
            continue;
        *eq = 0;
        apply(s, trim(p), trim(eq + 1));
    }
    fclose(f);
}

/* The TUI edits what is in the file, without this run's overrides. */
void amped2_settings_load_file_only(void)
{
    defaults(&g_amped2_settings);
    load_file(&g_amped2_settings);
}

void amped2_settings_load(void)
{
    static const char *const keys[] = { "resolution_scale", "aspect_ratio", "framerate",
                                        "frame_limit", "window_mode", "display" };
    Amped2Settings *s = &g_amped2_settings;
    char env[64];
    size_t i, k;

    amped2_settings_load_file_only();
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        const char *v;
        snprintf(env, sizeof env, "AMPED2_%s", keys[i]);
        for (k = 7; env[k]; k++)
            env[k] = (char)toupper((unsigned char)env[k]);
        if ((v = getenv(env)) && *v)
            apply(s, keys[i], v);
    }
    fprintf(stderr, "[SETTINGS] %s: scale %dx, aspect %s, %s fps (limit %d), %s, display %d\n",
            amped2_settings_path(), s->resolution_scale, amped2_aspect_name(s->aspect_ratio),
            k_fps_names[s->framerate], s->frame_limit, k_window_names[s->window_mode],
            s->display);
}

/* mkdir -p for the file's directory. */
static void make_parent_dirs(const char *path)
{
    char buf[1024];
    size_t i;

    snprintf(buf, sizeof buf, "%s", path);
    for (i = 1; buf[i]; i++)
        if (buf[i] == '/' || buf[i] == '\\') {
            char c = buf[i];
            buf[i] = 0;
            if (MKDIR(buf) != 0 && errno != EEXIST) {
                buf[i] = c;
                return;
            }
            buf[i] = c;
        }
}

int amped2_settings_save(void)
{
    const Amped2Settings *s = &g_amped2_settings;
    const char *path = amped2_settings_path();
    FILE *f;

    make_parent_dirs(path);
    if (!(f = fopen(path, "w")))
        return -1;
    fprintf(f, "# Amped 2 Recomp settings, written by `amped2_recomp --settings` (scripts/run.sh --settings).\n"
               "# Hand edits are fine; unknown keys are ignored.\n\n"
               "[graphics]\n");
    fprintf(f, "# 1-%d: internal resolution as a multiple of 640x480\n", AMPED2_SCALE_MAX);
    fprintf(f, "resolution_scale = %d\n", s->resolution_scale);
    fprintf(f, "# 4:3 (original), 16:9, 16:10, 21:9\n");
    fprintf(f, "aspect_ratio = %s\n", amped2_aspect_name(s->aspect_ratio));
    fprintf(f, "# original (60 fps, as on the Xbox) or unlocked\n");
    fprintf(f, "framerate = %s\n", k_fps_names[s->framerate]);
    fprintf(f, "# unlocked only: display (the monitor's refresh rate), none, or frames per second\n");
    if (s->frame_limit == 0)
        fprintf(f, "frame_limit = display\n");
    else if (s->frame_limit < 0)
        fprintf(f, "frame_limit = none\n");
    else
        fprintf(f, "frame_limit = %d\n", s->frame_limit);
    fprintf(f, "\n[display]\n");
    fprintf(f, "# windowed, fullscreen or borderless\n");
    fprintf(f, "window_mode = %s\n", k_window_names[s->window_mode]);
    fprintf(f, "# monitor for fullscreen / borderless (0 = the first)\n");
    fprintf(f, "display = %d\n", s->display);
    return fclose(f) == 0 ? 0 : -1;
}
