#include "presets.h"

#include "util.h"

#include <stdlib.h>
#include <string.h>

/* The customary set that classic players shipped with. */
const Preset presets[] = {
    { "Flat",               {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 } },
    { "Classical",          {   0,   0,   0,   0,   0,   0, -72, -72, -72, -96 } },
    { "Club",               {   0,   0,  80,  56,  56,  56,  32,   0,   0,   0 } },
    { "Dance",              {  96,  72,  24,   0,   0, -56, -72, -72,   0,   0 } },
    { "Full Bass",          {  96,  96,  96,  56,  16, -40, -80,-104,-112,-112 } },
    { "Full Bass & Treble", {  72,  56,   0, -72, -48,  16,  80, 112, 120, 120 } },
    { "Full Treble",        { -96, -96, -96, -40,  24, 112, 120, 120, 120, 120 } },
    { "Headphones",         {  48, 112,  56, -32, -24,  16,  48,  96, 120, 120 } },
    { "Large Hall",         { 104, 104,  56,  56,   0, -48, -48, -48,   0,   0 } },
    { "Live",               { -48,   0,  40,  56,  56,  56,  40,  24,  24,  24 } },
    { "Party",              {  72,  72,   0,   0,   0,   0,   0,   0,  72,  72 } },
    { "Pop",                { -16,  48,  72,  80,  56,   0, -24, -24, -16, -16 } },
    { "Reggae",             {   0,   0,   0, -56,   0,  64,  64,   0,   0,   0 } },
    { "Rock",               {  80,  48, -56, -80, -32,  40,  88, 112, 112, 112 } },
    { "Ska",                { -24, -48, -40,   0,  40,  56,  88,  96, 112,  96 } },
    { "Soft",               {  48,  16,   0, -24,   0,  40,  80,  96, 112, 120 } },
    { "Soft Rock",          {  40,  40,  24,   0, -40, -56, -32,   0,  24,  88 } },
    { "Techno",             {  80,  56,   0, -56, -48,   0,  80,  96,  96,  88 } },
};
const int preset_count = ARRAY_LEN(presets);

/* Words to look for in a genre, most specific first, and the preset each
 * one selects. */
static const struct { const char *word, *preset; } genre_words[] = {
    { "soft rock", "Soft Rock" }, { "classical", "Classical" }, { "opera", "Classical" },
    { "symphon", "Classical" }, { "baroque", "Classical" }, { "chamber", "Classical" },
    { "rock", "Rock" }, { "metal", "Rock" }, { "punk", "Rock" }, { "grunge", "Rock" },
    { "techno", "Techno" }, { "electro", "Techno" }, { "trance", "Techno" }, { "house", "Techno" },
    { "dance", "Dance" }, { "disco", "Dance" }, { "reggae", "Reggae" }, { "ska", "Ska" },
    { "club", "Club" }, { "hip", "Full Bass" }, { "rap", "Full Bass" }, { "pop", "Pop" },
    { "jazz", "Soft" }, { "blues", "Soft" }, { "folk", "Soft" }, { "acoustic", "Soft" },
};

/* Old tags store the genre as a number from the ID3v1 list. */
static const struct { int id; const char *name; } genre_ids[] = {
    { 0, "blues" }, { 1, "rock" }, { 3, "dance" }, { 4, "disco" }, { 7, "hip-hop" }, { 8, "jazz" },
    { 9, "metal" }, { 13, "pop" }, { 15, "rap" }, { 16, "reggae" }, { 17, "rock" }, { 18, "techno" },
    { 20, "rock" }, { 21, "ska" }, { 31, "trance" }, { 32, "classical" }, { 35, "house" },
    { 40, "rock" }, { 43, "punk" }, { 52, "electronic" }, { 79, "rock" }, { 80, "folk" },
    { 99, "acoustic" }, { 103, "opera" }, { 104, "chamber" }, { 106, "symphony" }, { 112, "club" },
};

static int find_preset(const char *name)
{
    int i;

    for (i = 0; i < preset_count; i++)
        if (strcmp(presets[i].name, name) == 0)
            return i;
    return -1;
}

int preset_for_genre(const char *genre)
{
    char lower[64];
    size_t i;

    /* "(17)" or "17": translate the number into a word first. */
    if (genre[0] == '(' || (genre[0] >= '0' && genre[0] <= '9')) {
        int id = atoi(genre + (genre[0] == '('));

        genre = "";
        for (i = 0; i < sizeof genre_ids / sizeof genre_ids[0]; i++)
            if (genre_ids[i].id == id)
                genre = genre_ids[i].name;
    }
    for (i = 0; genre[i] && i + 1 < sizeof lower; i++)
        lower[i] = genre[i] >= 'A' && genre[i] <= 'Z' ? (char)(genre[i] + ('a' - 'A')) : genre[i];
    lower[i] = '\0';

    for (i = 0; i < sizeof genre_words / sizeof genre_words[0]; i++)
        if (strstr(lower, genre_words[i].word))
            return find_preset(genre_words[i].preset);
    return -1;
}
