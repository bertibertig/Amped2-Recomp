/*
 * Graphics settings: one INI file, read at start-up, written by the settings
 * TUI (settings_tui.c, `amped2_recomp --settings`).
 *
 * File: $AMPED2_SETTINGS, else $XDG_CONFIG_HOME/amped2-recomp/settings.ini
 * (~/.config/... without XDG_CONFIG_HOME), on Windows
 * %APPDATA%\amped2-recomp\settings.ini. A missing file means the defaults,
 * which are the original look: 1x, 4:3, 60 fps, windowed.
 *
 * Environment overrides for one run (testing, scripts), same names as the
 * keys upper-cased with an AMPED2_ prefix: AMPED2_RESOLUTION_SCALE=3,
 * AMPED2_ASPECT_RATIO=16:9, AMPED2_FRAMERATE=unlocked, AMPED2_FRAME_LIMIT=144,
 * AMPED2_WINDOW_MODE=borderless, AMPED2_DISPLAY=1.
 */
#ifndef AMPED2_SETTINGS_H
#define AMPED2_SETTINGS_H

enum { AMPED2_ASPECT_4_3, AMPED2_ASPECT_16_9, AMPED2_ASPECT_16_10, AMPED2_ASPECT_21_9,
       AMPED2_ASPECT_COUNT };
enum { AMPED2_FPS_ORIGINAL, AMPED2_FPS_UNLOCKED, AMPED2_FPS_COUNT };
enum { AMPED2_WINDOWED, AMPED2_FULLSCREEN, AMPED2_BORDERLESS, AMPED2_WINDOW_COUNT };

#define AMPED2_SCALE_MAX 5

typedef struct {
    int resolution_scale;   /* 1 .. AMPED2_SCALE_MAX: internal resolution, x 640x480 */
    int aspect_ratio;       /* AMPED2_ASPECT_* */
    int framerate;          /* AMPED2_FPS_* */
    int frame_limit;        /* unlocked: frames per second, 0 = the display's refresh rate,
                             * -1 = no limit */
    int window_mode;        /* AMPED2_WINDOW* */
    int display;            /* SDL display index for fullscreen / borderless */
} Amped2Settings;

extern Amped2Settings g_amped2_settings;

/* Defaults, then the file, then the environment overrides. Call once, early. */
void amped2_settings_load(void);

/* Write the current values (not the environment overrides' effect on them --
 * the TUI loads without overrides). 0 on success. */
int amped2_settings_save(void);

/* The file in use. */
const char *amped2_settings_path(void);

/* Width / height of the aspect ratio, e.g. 16/9. */
double amped2_aspect_value(int aspect);
const char *amped2_aspect_name(int aspect);

/* The interactive settings screen in the terminal. Returns 1 when the player
 * chose to start the game, 0 to quit. */
int amped2_settings_tui(void);

#endif /* AMPED2_SETTINGS_H */
