/* The colour of the built-in look. One base colour is chosen (in the menu or
 * in amplitude.ini); the related shades are derived from it so the result
 * always hangs together. The neutral greys of the window body are fixed. */
#ifndef THEME_H
#define THEME_H

#include <stdint.h>

#define THEME_DEFAULT_COLOR 0x6CC4FF

/* The fixed greys every part of the built-in look is drawn with. */
#define COL_BODY      0x2B3040  /* window and button faces */
#define COL_LIGHT     0x4A5268  /* raised edges, grip lines */
#define COL_DARK      0x161922  /* sunken edges */
#define COL_TITLEBAR  0x1C2030
#define COL_ICON      0xC8D0E0  /* transport and close symbols */

typedef struct {
    uint32_t base;          /* the chosen colour: text, digits, accents */
    uint32_t text_on;       /* label of a button that is switched on */
    uint32_t dim;           /* level bars and other quiet fills */
    uint32_t peak;          /* pale tint for the top of the spectrum bars */
    uint32_t display;       /* background of the display areas */
    uint32_t grid;          /* faint dots and centre lines on displays */
    uint32_t highlight;     /* selected row, hovered menu entry */
} Theme;

typedef struct {
    const char *name;
    uint32_t color;
} ThemePreset;

extern const ThemePreset theme_presets[];
extern const int theme_preset_count;

/* Never NULL: until theme_set() is called this is the default colour. */
const Theme *theme_get(void);
void theme_set(uint32_t base);

#endif
