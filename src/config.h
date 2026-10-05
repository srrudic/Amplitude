/* Settings remembered between runs, stored as a small INI-style file. */
#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_EQ_SLIDERS 11    /* preamp + 10 bands */

typedef struct {
    int volume;                 /* 0..100 */
    int balance;                /* -100..100 */
    int shuffle, repeat;
    int eq_on;
    int eq_auto;                /* pick a preset from each track's genre */
    int eq[CONFIG_EQ_SLIDERS];  /* -1000..1000 */
    int scale;                  /* percent (100..400), or 0 to follow the desktop */
    char skin[1024];            /* empty = built-in skin */
    int color;                  /* 0xRRGGBB colour of the built-in look */
    int track;                  /* playlist position */
    int vis_mode;               /* 0 spectrum, 1 oscilloscope, 2 off */
    int pl_w, pl_h;             /* playlist window size, unscaled pixels */
    /* Window layout, in unscaled pixels. The equaliser and playlist are
     * stored relative to the main window. */
    int has_layout;
    int main_x, main_y;
    int eq_visible, eq_x, eq_y;
    int pl_visible, pl_x, pl_y;
} Config;

void config_defaults(Config *config);
/* Missing or malformed entries keep their current values. */
int  config_load(Config *config, const char *path);
int  config_save(const Config *config, const char *path);

#endif
