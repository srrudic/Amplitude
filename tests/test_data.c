/* Settings, playlist and presets: the modules that are pure data. */
#include "config.h"
#include "playlist.h"
#include "presets.h"
#include "theme.h"
#include "test.h"

static void test_config_round_trip(void)
{
    Config saved, loaded;
    const char *path = test_path("amplitude.ini");
    int i;

    config_defaults(&saved);
    saved.volume = 37;
    saved.balance = -40;
    saved.shuffle = saved.eq_on = saved.eq_auto = 1;
    saved.scale = 175;
    saved.color = 0x0AB1C2;
    saved.track = 12;
    saved.vis_mode = 2;
    saved.pl_w = 325;
    saved.pl_h = 290;
    saved.has_layout = 1;
    saved.main_x = -5;
    saved.main_y = 300;
    saved.eq_visible = 1;
    saved.eq_y = 116;
    saved.pl_x = 275;
    for (i = 0; i < CONFIG_EQ_SLIDERS; i++)
        saved.eq[i] = i * 150 - 700;
    snprintf(saved.skin, sizeof saved.skin, "/skins/Some Skin = odd.wsz");
    CHECK(config_save(&saved, path));

    config_defaults(&loaded);
    CHECK(config_load(&loaded, path));
    CHECK(memcmp(&saved, &loaded, sizeof saved) == 0);
    CHECK_STR(loaded.skin, "/skins/Some Skin = odd.wsz");
}

static void test_config_tolerates_bad_input(void)
{
    Config config;
    const char *path = test_path("bad.ini");
    static const char text[] =
        "volume=9999\nbalance=abc\nnonsense\n=\nscale_percent=-3\neq=5000,-5000,x\nunknown=1\n"
        "pl_w=1\npl_h=99999999999999999999\ncolor=#12345\ncolor=zzzzzz\ncolor=1234567\nvolume";

    test_write(path, text, sizeof text - 1);
    config_defaults(&config);
    CHECK(config_load(&config, path));
    CHECK_INT(config.volume, 100);          /* clamped */
    CHECK_INT(config.scale, 0);
    CHECK_INT(config.eq[0], 1000);
    CHECK_INT(config.eq[1], -1000);
    CHECK_INT(config.pl_w, 275);
    CHECK(config.pl_h <= 2000);
    CHECK_INT(config.color, THEME_DEFAULT_COLOR);      /* malformed colours are ignored */
    CHECK(!config_load(&config, test_path("missing.ini")));

    /* A colour may be written with or without "#", in either letter case. */
    test_write(path, "color=ffb347\n", 13);
    CHECK(config_load(&config, path));
    CHECK_INT(config.color, 0xFFB347);
    test_write(path, "color=#4CE0B3\n", 14);
    CHECK(config_load(&config, path));
    CHECK_INT(config.color, 0x4CE0B3);
}

static int breaths;

static void count_breath(void)
{
    breaths++;
}

/* The whole of a small text file. */
static const char *read_text(const char *path)
{
    static char text[4096];
    FILE *f = fopen(path, "rb");
    size_t size = f ? fread(text, 1, sizeof text - 1, f) : 0;

    if (f)
        fclose(f);
    text[size] = '\0';
    return text;
}

/* A shell command to be run in a directory. */
static const char *command_in(const char *dir, const char *command)
{
    static char line[1024];

    snprintf(line, sizeof line, "cd '%s' && %s", dir, command);
    return line;
}

static void test_playlist(void)
{
    const char *list = test_path("list.m3u"), *copy = test_path("copy.m3u");
    char expected[600];
    static const char m3u[] =
        "\xEF\xBB\xBF#EXTM3U\r\n#EXTINF:1,ignored\r\nsub/relative song.mp3\r\n\r\n/abs/Other.Name.flac\n";

    CHECK_INT(playlist_add("/music/Artist - Title.mp3"), 0);
    CHECK_INT(playlist_add("/music/noext"), 1);
    CHECK_INT(playlist_add("C:\\Music\\Windows Path.wav"), 2);
    CHECK_INT(playlist_count(), 3);
    /* No tags (the files do not exist): the title is the bare file name. */
    CHECK_STR(playlist_get(0)->title, "Artist - Title");
    CHECK_STR(playlist_get(1)->title, "noext");
    CHECK_STR(playlist_get(2)->title, "Windows Path");
    CHECK_INT(playlist_get(0)->length, -1);
    CHECK(playlist_get(3) == NULL && playlist_get(-1) == NULL);

    playlist_set_length(1, 200);
    playlist_select_range(2, 1);
    CHECK(!playlist_get(0)->selected && playlist_get(1)->selected && playlist_get(2)->selected);
    playlist_select(1, 0);
    playlist_select_all();
    CHECK(playlist_get(0)->selected && playlist_get(1)->selected);
    playlist_select_range(-1, -1);
    CHECK(!playlist_get(0)->selected && !playlist_get(2)->selected);
    playlist_select(1, 1);
    playlist_invert_selection();
    CHECK(playlist_get(0)->selected && !playlist_get(1)->selected && playlist_get(2)->selected);
    playlist_select_range(-1, -1);

    /* Reordering reports where the followed track went, and a track keeps
     * its length and selection when it moves. Titles: "Artist - Title",
     * "noext", "Windows Path"; file names sort the same way here. */
    playlist_select(2, 1);
    CHECK_INT(playlist_reverse(2), 0);
    CHECK_STR(playlist_get(0)->title, "Windows Path");
    CHECK(playlist_get(0)->selected && !playlist_get(2)->selected);
    CHECK_INT(playlist_get(1)->length, 200);
    CHECK_INT(playlist_sort(PLAYLIST_BY_TITLE, 0), 2);
    CHECK_STR(playlist_get(0)->title, "Artist - Title");
    CHECK_STR(playlist_get(1)->title, "noext");
    CHECK_INT(playlist_reverse(-1), -1);
    CHECK_INT(playlist_sort(PLAYLIST_BY_FILENAME, 2), 0);
    CHECK_STR(playlist_get(2)->title, "Windows Path");
    /* By path: "/music/Artist...", "/music/noext", "C:\\Music..." */
    CHECK_INT(playlist_sort(PLAYLIST_BY_PATH, 1), 1);
    CHECK_STR(playlist_get(2)->title, "Windows Path");
    CHECK_INT(playlist_randomize(1) >= 0 && playlist_count() == 3, 1);
    playlist_sort(PLAYLIST_BY_TITLE, -1);
    CHECK_INT(playlist_get(1)->length, 200);
    playlist_select_range(-1, -1);

    /* The play queue: places count up from 1 in the order tracks were
     * queued, close up when one leaves, and travel with a track that moves. */
    CHECK_INT(playlist_queue_head(), -1);
    playlist_queue_toggle(2);
    playlist_queue_toggle(0);
    playlist_queue_toggle(1);
    CHECK(playlist_get(2)->queue == 1 && playlist_get(0)->queue == 2 && playlist_get(1)->queue == 3);
    CHECK_INT(playlist_queue_head(), 2);
    playlist_queue_toggle(0);                   /* a second toggle takes it out */
    CHECK(playlist_get(2)->queue == 1 && playlist_get(0)->queue == 0 && playlist_get(1)->queue == 2);
    playlist_reverse(-1);                       /* 2 is now first */
    CHECK_INT(playlist_queue_head(), 0);
    playlist_reverse(-1);
    playlist_dequeue(2);
    CHECK_INT(playlist_queue_head(), 1);
    playlist_dequeue(2);                        /* not queued: nothing happens */
    CHECK_INT(playlist_get(1)->queue, 1);
    playlist_queue_toggle(0);
    playlist_remove(1);                         /* removing the head promotes the next */
    CHECK_INT(playlist_count(), 2);
    CHECK_INT(playlist_queue_head(), 0);
    playlist_queue_clear();
    CHECK(playlist_queue_head() == -1 && playlist_get(0)->queue == 0);
    playlist_add("/music/noext");               /* put back what the next checks expect */
    playlist_sort(PLAYLIST_BY_TITLE, -1);
    playlist_set_length(1, 200);

    playlist_remove(0);
    CHECK_INT(playlist_count(), 2);
    CHECK_STR(playlist_get(0)->title, "noext");
    CHECK_INT(playlist_get(0)->length, 200);
    playlist_remove(7);                     /* out of range: ignored */
    CHECK_INT(playlist_count(), 2);
    playlist_free();
    CHECK_INT(playlist_count(), 0);

    /* M3U: byte order mark, comments, CRLF, relative and absolute entries. */
    test_write(list, m3u, sizeof m3u - 1);
    CHECK_INT(playlist_load(list, NULL), 2);
    snprintf(expected, sizeof expected, "%s/sub/relative song.mp3", test_dir);
    CHECK_STR(playlist_get(0)->path, expected);
    CHECK_STR(playlist_get(1)->path, "/abs/Other.Name.flac");
    CHECK_STR(playlist_get(1)->title, "Other.Name");
    CHECK(playlist_save(copy));
    playlist_free();
    CHECK_INT(playlist_load(copy, count_breath), 2);
    CHECK_INT(breaths, 0);                      /* too short a list to pause for */
    CHECK_STR(playlist_get(0)->path, expected);

    /* Saving: what lies in the playlist's folder or below it is written
     * relative to it, the rest in full; addresses and CD tracks as they are. */
    playlist_add("http://radio.example/live");
    playlist_add("cdda:///dev/sr0/3");
    snprintf(expected, sizeof expected, "%s/beside.mp3", test_dir);
    playlist_add(expected);
    snprintf(expected, sizeof expected, "%s-elsewhere/not below.mp3", test_dir);   /* only the name begins alike */
    playlist_add(expected);
    CHECK(playlist_save(copy));
    snprintf(expected, sizeof expected, "sub/relative song.mp3\n/abs/Other.Name.flac\nhttp://radio.example/live\n"
                                        "cdda:///dev/sr0/3\nbeside.mp3\n%s-elsewhere/not below.mp3\n", test_dir);
    CHECK_STR(read_text(copy), expected);
    /* Saved somewhere else, nothing is below it. */
    CHECK(system(command_in(test_dir, "mkdir other")) == 0);
    CHECK(playlist_save(test_path("other/far.m3u")));
    CHECK(strstr(read_text(test_path("other/far.m3u")), "/sub/relative song.mp3\n/abs/") != NULL);
    CHECK(strstr(read_text(test_path("other/far.m3u")), "/beside.mp3\n") != NULL);
    playlist_free();

    /* The folder is moved, playlist and all: the relative entries follow. */
    CHECK(system(command_in(test_dir, "mkdir moved && cp copy.m3u moved/")) == 0);
    CHECK_INT(playlist_load(test_path("moved/copy.m3u"), NULL), 6);
    snprintf(expected, sizeof expected, "%s/moved/sub/relative song.mp3", test_dir);
    CHECK_STR(playlist_get(0)->path, expected);
    snprintf(expected, sizeof expected, "%s/moved/beside.mp3", test_dir);
    CHECK_STR(playlist_get(4)->path, expected);
    CHECK_STR(playlist_get(1)->path, "/abs/Other.Name.flac");
    playlist_free();

    /* A list written on Windows separates folders its own way. Here that
     * is taken for a separator only if no file is called exactly so. */
    CHECK(system(command_in(test_dir, "mkdir -p win/a && touch 'win/a/b.mp3' 'win/odd\\name.mp3'")) == 0);
    {
        static const char windows_list[] = "a\\b.mp3\r\nodd\\name.mp3\r\n";

        test_write(test_path("win/list.m3u"), windows_list, sizeof windows_list - 1);
    }
    CHECK_INT(playlist_load(test_path("win/list.m3u"), NULL), 2);
    snprintf(expected, sizeof expected, "%s/win/a/b.mp3", test_dir);
    CHECK_STR(playlist_get(0)->path, expected);
    snprintf(expected, sizeof expected, "%s/win/odd\\name.mp3", test_dir);
    CHECK_STR(playlist_get(1)->path, expected);
    playlist_free();
    CHECK_INT(playlist_load(test_path("missing.m3u"), NULL), 0);
}

static const char *preset_name(const char *genre)
{
    int index = preset_for_genre(genre);

    return index < 0 ? "(none)" : presets[index].name;
}

static void test_presets(void)
{
    int i, band;

    CHECK(preset_count >= 10);
    CHECK_STR(presets[0].name, "Flat");
    for (i = 0; i < preset_count; i++)
        for (band = 0; band < PRESET_BANDS; band++)
            CHECK(presets[i].gain[band] >= -120 && presets[i].gain[band] <= 120);

    CHECK_STR(preset_name("Rock"), "Rock");
    CHECK_STR(preset_name("(17)"), "Rock");
    CHECK_STR(preset_name("32"), "Classical");
    CHECK_STR(preset_name("Soft Rock"), "Soft Rock");
    CHECK_STR(preset_name("Hard Rock;Metal"), "Rock");
    CHECK_STR(preset_name("ELECTRONIC"), "Techno");
    CHECK_STR(preset_name("Drum & Bass"), "(none)");
    CHECK_STR(preset_name(""), "(none)");
    CHECK_STR(preset_name("(255)"), "(none)");
}

static unsigned brightness(uint32_t c)
{
    return (c >> 16 & 255) + (c >> 8 & 255) + (c & 255);
}

static void test_theme(void)
{
    const Theme *theme = theme_get();
    int i;

    /* Before anything is chosen, the default colour applies. */
    CHECK(theme->base == THEME_DEFAULT_COLOR);
    CHECK(theme_presets[0].color == THEME_DEFAULT_COLOR);
    CHECK(theme_preset_count >= 4);

    for (i = 0; i < theme_preset_count; i++) {
        theme_set(theme_presets[i].color);
        CHECK(theme->base == theme_presets[i].color);
        /* Shades keep their order of brightness, whatever the colour, so
         * text stays readable on the display and bars stay visible. */
        CHECK(brightness(theme->display) < brightness(theme->grid));
        CHECK(brightness(theme->grid) < brightness(theme->dim));
        CHECK(brightness(theme->dim) < brightness(theme->base));
        CHECK(brightness(theme->base) < brightness(theme->peak));
        CHECK(brightness(theme->display) < 60);
        CHECK(theme->text_on != theme->base);       /* "on" always differs from "off" */
    }
    theme_set(0xFFFFFF);
    CHECK(theme->text_on != 0xFFFFFF);
    theme_set(0x1FFFFFFF);                          /* extra bits are dropped */
    CHECK(theme->base == 0xFFFFFF);
    theme_set(THEME_DEFAULT_COLOR);
}

int main(void)
{
    test_begin("data");
    test_theme();
    test_config_round_trip();
    test_config_tolerates_bad_input();
    test_playlist();
    test_presets();
    return test_end();
}
