/* Built-in equaliser presets. */
#ifndef PRESETS_H
#define PRESETS_H

#define PRESET_BANDS 10

typedef struct {
    const char *name;
    signed char gain[PRESET_BANDS];     /* tenths of a dB, 60 Hz .. 16 kHz */
} Preset;

extern const Preset presets[];
extern const int preset_count;

/* Picks the preset that suits a genre tag ("Rock", "(17)", "Hard Rock;Metal",
 * ...). Returns its index, or -1 if nothing fits. */
int preset_for_genre(const char *genre);

#endif
