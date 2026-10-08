#include "config.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(Config *config)
{
    memset(config, 0, sizeof *config);
    config->volume = 80;
    config->cd_names = 1;
    config->color = 0x6CC4FF;
    config->pl_w = 275;
    config->pl_h = 232;
}

static int clamp(long value, int min, int max)
{
    return value < min ? min : value > max ? max : (int)value;
}

int config_load(Config *config, const char *path)
{
    /* Plain integer settings: name, destination, allowed range. */
    const struct { const char *key; int *value; int min, max; } ints[] = {
        { "volume", &config->volume, 0, 100 },
        { "balance", &config->balance, -100, 100 },
        { "shuffle", &config->shuffle, 0, 1 },
        { "repeat", &config->repeat, 0, 1 },
        { "cd_names", &config->cd_names, 0, 1 },
        { "eq_on", &config->eq_on, 0, 1 },
        { "eq_auto", &config->eq_auto, 0, 1 },
        { "scale_percent", &config->scale, 0, 400 },
        { "track", &config->track, 0, 1000000 },
        { "vis_mode", &config->vis_mode, 0, 2 },
        { "pl_w", &config->pl_w, 275, 2000 },
        { "pl_h", &config->pl_h, 116, 2000 },
        { "has_layout", &config->has_layout, 0, 1 },
        { "main_x", &config->main_x, -32768, 32767 },
        { "main_y", &config->main_y, -32768, 32767 },
        { "eq_visible", &config->eq_visible, 0, 1 },
        { "eq_x", &config->eq_x, -32768, 32767 },
        { "eq_y", &config->eq_y, -32768, 32767 },
        { "pl_visible", &config->pl_visible, 0, 1 },
        { "pl_x", &config->pl_x, -32768, 32767 },
        { "pl_y", &config->pl_y, -32768, 32767 },
    };
    char line[1200];
    FILE *f = plat_fopen(path, "r");
    size_t i;

    if (!f)
        return 0;
    while (fgets(line, sizeof line, f)) {
        char *value = strchr(line, '=');
        size_t len;

        if (!value)
            continue;
        *value++ = '\0';
        len = strlen(value);
        while (len && (value[len - 1] == '\n' || value[len - 1] == '\r'))
            value[--len] = '\0';

        if (strcmp(line, "skin") == 0) {
            snprintf(config->skin, sizeof config->skin, "%s", value);
        } else if (strcmp(line, "color") == 0) {
            /* "#RRGGBB", with or without the "#"; anything else is ignored. */
            char *end;
            long color = strtol(value + (*value == '#'), &end, 16);

            if (end - value - (*value == '#') == 6 && !*end)
                config->color = (int)color;
        } else if (strcmp(line, "eq") == 0) {
            for (i = 0; i < CONFIG_EQ_SLIDERS && *value; i++) {
                config->eq[i] = clamp(strtol(value, &value, 10), -1000, 1000);
                if (*value == ',')
                    value++;
            }
        } else {
            for (i = 0; i < sizeof ints / sizeof ints[0]; i++)
                if (strcmp(line, ints[i].key) == 0)
                    *ints[i].value = clamp(strtol(value, NULL, 10), ints[i].min, ints[i].max);
        }
    }
    fclose(f);
    return 1;
}

int config_save(const Config *config, const char *path)
{
    FILE *f = plat_fopen(path, "w");
    int i;

    if (!f)
        return 0;
    fprintf(f, "volume=%d\nbalance=%d\nshuffle=%d\nrepeat=%d\n", config->volume, config->balance,
            config->shuffle, config->repeat);
    fprintf(f, "cd_names=%d\n", config->cd_names);
    fprintf(f, "eq_on=%d\neq_auto=%d\neq=", config->eq_on, config->eq_auto);
    for (i = 0; i < CONFIG_EQ_SLIDERS; i++)
        fprintf(f, "%s%d", i ? "," : "", config->eq[i]);
    fprintf(f, "\nscale_percent=%d\nskin=%s\ncolor=#%06X\ntrack=%d\n", config->scale, config->skin,
            (unsigned)config->color & 0xFFFFFF, config->track);
    fprintf(f, "has_layout=%d\nmain_x=%d\nmain_y=%d\n", config->has_layout, config->main_x, config->main_y);
    fprintf(f, "eq_visible=%d\neq_x=%d\neq_y=%d\n", config->eq_visible, config->eq_x, config->eq_y);
    fprintf(f, "pl_visible=%d\npl_x=%d\npl_y=%d\n", config->pl_visible, config->pl_x, config->pl_y);
    fprintf(f, "pl_w=%d\npl_h=%d\nvis_mode=%d\n", config->pl_w, config->pl_h, config->vis_mode);
    return fclose(f) == 0;
}
