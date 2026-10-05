#include "theme.h"

#include "util.h"

const ThemePreset theme_presets[] = {
    { "Light blue", THEME_DEFAULT_COLOR },
    { "Green",      0x4CE0B3 },
    { "Amber",      0xFFB347 },
    { "Yellow",     0xFFE066 },
    { "Red",        0xFF6B5E },
    { "Pink",       0xFF7EB6 },
    { "Violet",     0xB18CFF },
    { "White",      0xE8ECF4 },
};
const int theme_preset_count = ARRAY_LEN(theme_presets);

static Theme theme;
static int theme_ready;

/* Blends two colours; `percent` is the share of b. */
static uint32_t mix(uint32_t a, uint32_t b, unsigned percent)
{
    uint32_t out = 0;
    int shift;

    for (shift = 0; shift <= 16; shift += 8)
        out |= (((a >> shift & 255) * (100 - percent) + (b >> shift & 255) * percent) / 100) << shift;
    return out;
}

void theme_set(uint32_t base)
{
    unsigned brightness;

    base &= 0xFFFFFF;
    brightness = ((base >> 16 & 255) * 3 + (base >> 8 & 255) * 6 + (base & 255)) / 10;

    theme.base = base;
    /* "On" labels are white, unless the colour is itself close to white. */
    theme.text_on = brightness > 228 ? 0xFFC857 : 0xFFFFFF;
    theme.display = mix(0x060606, base, 4);
    theme.dim = mix(base, theme.display, 60);
    theme.peak = mix(base, 0xFFFFFF, 65);
    theme.grid = mix(theme.display, base, 10);
    theme.highlight = mix(0x161C28, base, 22);
    theme_ready = 1;
}

const Theme *theme_get(void)
{
    if (!theme_ready)
        theme_set(THEME_DEFAULT_COLOR);
    return &theme;
}
