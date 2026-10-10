/* MPRIS over D-Bus (see mpris.h).
 *
 * The D-Bus library is not linked: it is looked up when the player starts,
 * so the player still runs on systems without it, and nothing has to be
 * present at build time. The few functions and constants used are declared
 * here; the library's interface has been stable for many years.
 *
 * Only what controllers need is implemented: the transport commands, the
 * shuffle and repeat switches and the read-only properties that say what is
 * playing. Seeking, volume and the track list are left out. */
#include "mpris.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define OBJECT_PATH "/org/mpris/MediaPlayer2"
#define ROOT_IFACE  "org.mpris.MediaPlayer2"
#define PLAYER_IFACE "org.mpris.MediaPlayer2.Player"
#define PROPS_IFACE "org.freedesktop.DBus.Properties"
#define BUS_NAME    "org.mpris.MediaPlayer2.amplitude"
#define TRACK_PATH  "/org/mpris/MediaPlayer2/CurrentTrack"

/* From the D-Bus headers. */
typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef union { void *align; long long align2; char space[128]; } Iter;    /* DBusMessageIter, with room to spare */
typedef unsigned int dbus_bool_t;
enum { BUS_SESSION = 0, NAME_DO_NOT_QUEUE = 4, NAME_PRIMARY_OWNER = 1, MESSAGE_METHOD_CALL = 1 };
enum { T_STRING = 's', T_OBJECT_PATH = 'o', T_BOOLEAN = 'b', T_DOUBLE = 'd', T_INT64 = 'x',
       T_ARRAY = 'a', T_VARIANT = 'v', T_DICT_ENTRY = 'e' };

static struct {
    DBusConnection *(*bus_get)(int type, void *error);
    void (*set_exit_on_disconnect)(DBusConnection *, dbus_bool_t);
    int (*request_name)(DBusConnection *, const char *, unsigned, void *error);
    dbus_bool_t (*read_write)(DBusConnection *, int timeout_ms);
    DBusMessage *(*pop_message)(DBusConnection *);
    dbus_bool_t (*get_unix_fd)(DBusConnection *, int *);
    dbus_bool_t (*send)(DBusConnection *, DBusMessage *, unsigned *serial);
    void (*flush)(DBusConnection *);
    void (*connection_unref)(DBusConnection *);
    void (*message_unref)(DBusMessage *);
    int (*get_type)(DBusMessage *);
    const char *(*get_path)(DBusMessage *);
    const char *(*get_interface)(DBusMessage *);
    const char *(*get_member)(DBusMessage *);
    dbus_bool_t (*get_no_reply)(DBusMessage *);
    DBusMessage *(*new_method_return)(DBusMessage *);
    DBusMessage *(*new_error)(DBusMessage *, const char *name, const char *text);
    DBusMessage *(*new_signal)(const char *path, const char *iface, const char *name);
    dbus_bool_t (*iter_init)(DBusMessage *, Iter *);
    int (*iter_get_arg_type)(Iter *);
    void (*iter_get_basic)(Iter *, void *value);
    dbus_bool_t (*iter_next)(Iter *);
    void (*iter_recurse)(Iter *, Iter *sub);
    void (*iter_init_append)(DBusMessage *, Iter *);
    dbus_bool_t (*iter_append_basic)(Iter *, int type, const void *value);
    dbus_bool_t (*iter_open_container)(Iter *, int type, const char *signature, Iter *sub);
    dbus_bool_t (*iter_close_container)(Iter *, Iter *sub);
} dbus;

static const struct { const char *name; void *slot; } symbols[] = {
    { "dbus_bus_get", &dbus.bus_get },
    { "dbus_connection_set_exit_on_disconnect", &dbus.set_exit_on_disconnect },
    { "dbus_bus_request_name", &dbus.request_name },
    { "dbus_connection_read_write", &dbus.read_write },
    { "dbus_connection_pop_message", &dbus.pop_message },
    { "dbus_connection_get_unix_fd", &dbus.get_unix_fd },
    { "dbus_connection_send", &dbus.send },
    { "dbus_connection_flush", &dbus.flush },
    { "dbus_connection_unref", &dbus.connection_unref },
    { "dbus_message_unref", &dbus.message_unref },
    { "dbus_message_get_type", &dbus.get_type },
    { "dbus_message_get_path", &dbus.get_path },
    { "dbus_message_get_interface", &dbus.get_interface },
    { "dbus_message_get_member", &dbus.get_member },
    { "dbus_message_get_no_reply", &dbus.get_no_reply },
    { "dbus_message_new_method_return", &dbus.new_method_return },
    { "dbus_message_new_error", &dbus.new_error },
    { "dbus_message_new_signal", &dbus.new_signal },
    { "dbus_message_iter_init", &dbus.iter_init },
    { "dbus_message_iter_get_arg_type", &dbus.iter_get_arg_type },
    { "dbus_message_iter_get_basic", &dbus.iter_get_basic },
    { "dbus_message_iter_next", &dbus.iter_next },
    { "dbus_message_iter_recurse", &dbus.iter_recurse },
    { "dbus_message_iter_init_append", &dbus.iter_init_append },
    { "dbus_message_iter_append_basic", &dbus.iter_append_basic },
    { "dbus_message_iter_open_container", &dbus.iter_open_container },
    { "dbus_message_iter_close_container", &dbus.iter_close_container },
};

static void *library;
static DBusConnection *bus;

/* What controllers are told. */
static int state;
static char title[512];
static double position, length;
static int shuffle, repeat;

/* --- Properties ------------------------------------------------------------- */

static void variant(Iter *it, int type, const void *value)
{
    char signature[2] = { (char)type, '\0' };
    Iter sub;

    dbus.iter_open_container(it, T_VARIANT, signature, &sub);
    dbus.iter_append_basic(&sub, type, value);
    dbus.iter_close_container(it, &sub);
}

/* One "name: variant" entry of a dictionary. */
static void entry(Iter *dict, const char *name, int type, const void *value)
{
    Iter item;

    dbus.iter_open_container(dict, T_DICT_ENTRY, NULL, &item);
    dbus.iter_append_basic(&item, T_STRING, &name);
    variant(&item, type, value);
    dbus.iter_close_container(dict, &item);
}

static void empty_string_list(Iter *it)
{
    Iter outer, list;

    dbus.iter_open_container(it, T_VARIANT, "as", &outer);
    dbus.iter_open_container(&outer, T_ARRAY, "s", &list);
    dbus.iter_close_container(&outer, &list);
    dbus.iter_close_container(it, &outer);
}

/* The Metadata property: the track's id, title and length. */
static void metadata(Iter *it)
{
    const char *track = TRACK_PATH, *name = title;
    long long microseconds = (long long)(length * 1e6);
    Iter outer, dict;

    dbus.iter_open_container(it, T_VARIANT, "a{sv}", &outer);
    dbus.iter_open_container(&outer, T_ARRAY, "{sv}", &dict);
    if (state || title[0]) {
        entry(&dict, "mpris:trackid", T_OBJECT_PATH, &track);
        entry(&dict, "xesam:title", T_STRING, &name);
        if (length > 0)
            entry(&dict, "mpris:length", T_INT64, &microseconds);
    }
    dbus.iter_close_container(&outer, &dict);
    dbus.iter_close_container(it, &outer);
}

static const char *const root_properties[] = {
    "CanQuit", "CanRaise", "HasTrackList", "Identity", "DesktopEntry", "SupportedUriSchemes", "SupportedMimeTypes", NULL };
static const char *const player_properties[] = {
    "PlaybackStatus", "Metadata", "Position", "Rate", "MinimumRate", "MaximumRate", "Volume",
    "Shuffle", "LoopStatus", "CanGoNext", "CanGoPrevious", "CanPlay", "CanPause", "CanSeek", "CanControl", NULL };

/* Appends the value of a property as a variant. Returns 0 for a name that
 * is not one of ours. */
static int property(Iter *it, const char *name)
{
    static const char *const statuses[] = { "Stopped", "Playing", "Paused" };
    const char *text;
    dbus_bool_t yes = 1, no = 0, shuffled = shuffle != 0;
    double one = 1.0;
    long long microseconds = (long long)(position * 1e6);

    if (!strcmp(name, "PlaybackStatus")) {
        text = statuses[state >= 0 && state <= 2 ? state : 0];
        variant(it, T_STRING, &text);
    } else if (!strcmp(name, "Metadata")) {
        metadata(it);
    } else if (!strcmp(name, "Shuffle")) {
        variant(it, T_BOOLEAN, &shuffled);
    } else if (!strcmp(name, "LoopStatus")) {
        text = repeat == 2 ? "Track" : repeat ? "Playlist" : "None";
        variant(it, T_STRING, &text);
    } else if (!strcmp(name, "Position")) {
        variant(it, T_INT64, &microseconds);
    } else if (!strcmp(name, "Rate") || !strcmp(name, "MinimumRate") || !strcmp(name, "MaximumRate") ||
               !strcmp(name, "Volume")) {
        variant(it, T_DOUBLE, &one);
    } else if (!strcmp(name, "CanGoNext") || !strcmp(name, "CanGoPrevious") || !strcmp(name, "CanPlay") ||
               !strcmp(name, "CanPause") || !strcmp(name, "CanControl") || !strcmp(name, "CanQuit") ||
               !strcmp(name, "CanRaise")) {
        variant(it, T_BOOLEAN, &yes);
    } else if (!strcmp(name, "CanSeek") || !strcmp(name, "HasTrackList")) {
        variant(it, T_BOOLEAN, &no);
    } else if (!strcmp(name, "Identity")) {
        text = "Amplitude";
        variant(it, T_STRING, &text);
    } else if (!strcmp(name, "DesktopEntry")) {
        text = "amplitude";
        variant(it, T_STRING, &text);
    } else if (!strcmp(name, "SupportedUriSchemes") || !strcmp(name, "SupportedMimeTypes")) {
        empty_string_list(it);
    } else {
        return 0;
    }
    return 1;
}

static void all_properties(Iter *it, const char *const *names)
{
    Iter dict;

    dbus.iter_open_container(it, T_ARRAY, "{sv}", &dict);
    for (; *names; names++) {
        Iter item;

        dbus.iter_open_container(&dict, T_DICT_ENTRY, NULL, &item);
        dbus.iter_append_basic(&item, T_STRING, names);
        property(&item, *names);
        dbus.iter_close_container(&dict, &item);
    }
    dbus.iter_close_container(it, &dict);
}

/* Tells whoever listens that the status, the track or a switch changed. */
static void announce(void)
{
    static const char *const changed[] = { "PlaybackStatus", "Metadata", "Shuffle", "LoopStatus", NULL };
    const char *iface = PLAYER_IFACE;
    DBusMessage *signal = dbus.new_signal(OBJECT_PATH, PROPS_IFACE, "PropertiesChanged");
    Iter it, none;

    if (!signal)
        return;
    dbus.iter_init_append(signal, &it);
    dbus.iter_append_basic(&it, T_STRING, &iface);
    all_properties(&it, changed);
    dbus.iter_open_container(&it, T_ARRAY, "s", &none);     /* invalidated properties: none */
    dbus.iter_close_container(&it, &none);
    dbus.send(bus, signal, NULL);
    dbus.message_unref(signal);
    dbus.flush(bus);
}

/* --- Requests --------------------------------------------------------------- */

/* Reads up to two string arguments of a call. */
static void string_arguments(DBusMessage *message, const char **first, const char **second)
{
    Iter it;

    *first = *second = "";
    if (!dbus.iter_init(message, &it) || dbus.iter_get_arg_type(&it) != T_STRING)
        return;
    dbus.iter_get_basic(&it, first);
    if (dbus.iter_next(&it) && dbus.iter_get_arg_type(&it) == T_STRING)
        dbus.iter_get_basic(&it, second);
}

/* Properties.Set(interface, name, variant): the two switches are all that
 * can be set. Returns 0 for anything else. The new state is announced once
 * the player has taken it up (see mpris_update()). */
static int set_property(DBusMessage *message, void (*on_command)(int command))
{
    const char *name, *text;
    dbus_bool_t on;
    Iter it, value;

    if (!dbus.iter_init(message, &it) || dbus.iter_get_arg_type(&it) != T_STRING || !dbus.iter_next(&it) ||
        dbus.iter_get_arg_type(&it) != T_STRING)
        return 0;
    dbus.iter_get_basic(&it, &name);
    if (!dbus.iter_next(&it) || dbus.iter_get_arg_type(&it) != T_VARIANT)
        return 0;
    dbus.iter_recurse(&it, &value);
    if (!strcmp(name, "Shuffle") && dbus.iter_get_arg_type(&value) == T_BOOLEAN) {
        dbus.iter_get_basic(&value, &on);
        on_command(on ? MPRIS_SHUFFLE_ON : MPRIS_SHUFFLE_OFF);
    } else if (!strcmp(name, "LoopStatus") && dbus.iter_get_arg_type(&value) == T_STRING) {
        dbus.iter_get_basic(&value, &text);
        on_command(!strcmp(text, "Track") ? MPRIS_REPEAT_TRACK :
                   !strcmp(text, "Playlist") ? MPRIS_REPEAT_LIST : MPRIS_REPEAT_OFF);
    } else {
        return 0;
    }
    return 1;
}

static void handle(DBusMessage *message, void (*on_command)(int command))
{
    static const struct { const char *member; int command; } commands[] = {
        { "Play", MPRIS_PLAY }, { "Pause", MPRIS_PAUSE }, { "PlayPause", MPRIS_PLAY_PAUSE },
        { "Stop", MPRIS_STOP }, { "Next", MPRIS_NEXT }, { "Previous", MPRIS_PREVIOUS },
        { "Raise", MPRIS_RAISE }, { "Quit", MPRIS_QUIT } };
    const char *path = dbus.get_path(message), *iface = dbus.get_interface(message);
    const char *member = dbus.get_member(message), *wanted_iface, *wanted_name;
    DBusMessage *reply = NULL;
    Iter it;
    size_t i;

    if (dbus.get_type(message) != MESSAGE_METHOD_CALL || !path || !member || strcmp(path, OBJECT_PATH) != 0)
        return;
    if (!iface)
        iface = "";

    if (!strcmp(iface, PROPS_IFACE) && !strcmp(member, "GetAll")) {
        string_arguments(message, &wanted_iface, &wanted_name);
        reply = dbus.new_method_return(message);
        if (reply) {
            dbus.iter_init_append(reply, &it);
            all_properties(&it, !strcmp(wanted_iface, ROOT_IFACE) ? root_properties : player_properties);
        }
    } else if (!strcmp(iface, PROPS_IFACE) && !strcmp(member, "Get")) {
        string_arguments(message, &wanted_iface, &wanted_name);
        reply = dbus.new_method_return(message);
        if (reply) {
            dbus.iter_init_append(reply, &it);
            if (!property(&it, wanted_name)) {
                dbus.message_unref(reply);
                reply = dbus.new_error(message, "org.freedesktop.DBus.Error.UnknownProperty", wanted_name);
            }
        }
    } else if (!strcmp(iface, PROPS_IFACE) && !strcmp(member, "Set")) {
        if (set_property(message, on_command))
            reply = dbus.new_method_return(message);
    } else if (!strcmp(iface, PLAYER_IFACE) || !strcmp(iface, ROOT_IFACE) || !iface[0]) {
        for (i = 0; i < sizeof commands / sizeof commands[0]; i++) {
            if (!strcmp(member, commands[i].member)) {
                on_command(commands[i].command);
                reply = dbus.new_method_return(message);
                break;
            }
        }
    }
    if (!reply)     /* seeking, setting other properties and anything else */
        reply = dbus.new_error(message, "org.freedesktop.DBus.Error.NotSupported", member);
    if (reply) {
        if (!dbus.get_no_reply(message))
            dbus.send(bus, reply, NULL);
        dbus.message_unref(reply);
    }
}

/* --- The interface ----------------------------------------------------------- */

int mpris_init(void)
{
    char own_name[96];
    size_t i;

    library = dlopen("libdbus-1.so.3", RTLD_NOW);
    if (!library)
        return 0;
    for (i = 0; i < sizeof symbols / sizeof symbols[0]; i++) {
        void *address = dlsym(library, symbols[i].name);

        if (!address) {
            mpris_shutdown();
            return 0;
        }
        memcpy(symbols[i].slot, &address, sizeof address);     /* a function pointer, stored as such */
    }
    bus = dbus.bus_get(BUS_SESSION, NULL);
    if (!bus) {
        mpris_shutdown();
        return 0;
    }
    dbus.set_exit_on_disconnect(bus, 0);    /* losing the bus must not end the player */
    /* A second player (one with other settings) gets a name of its own, as
     * the specification asks. */
    if (dbus.request_name(bus, BUS_NAME, NAME_DO_NOT_QUEUE, NULL) != NAME_PRIMARY_OWNER) {
        snprintf(own_name, sizeof own_name, BUS_NAME ".instance%ld", (long)getpid());
        dbus.request_name(bus, own_name, NAME_DO_NOT_QUEUE, NULL);
    }
    dbus.flush(bus);
    return 1;
}

void mpris_shutdown(void)
{
    if (bus) {
        dbus.flush(bus);
        dbus.connection_unref(bus);
    }
    bus = NULL;
    if (library)
        dlclose(library);
    library = NULL;
}

int mpris_fd(void)
{
    int fd = -1;

    if (!bus || !dbus.get_unix_fd(bus, &fd))
        return -1;
    return fd;
}

void mpris_poll(void (*on_command)(int command))
{
    DBusMessage *message;

    if (!bus)
        return;
    dbus.read_write(bus, 0);
    while ((message = dbus.pop_message(bus)) != NULL) {
        handle(message, on_command);
        dbus.message_unref(message);
    }
    dbus.flush(bus);
}

void mpris_update(int new_state, const char *new_title, double new_position, double new_length,
                  int new_shuffle, int new_repeat)
{
    int changed = new_state != state || strcmp(new_title, title) != 0 ||
                  (long long)new_length != (long long)length || new_shuffle != shuffle || new_repeat != repeat;

    state = new_state;
    position = new_position;
    length = new_length;
    shuffle = new_shuffle;
    repeat = new_repeat;
    if (changed) {
        size_t n;

        snprintf(title, sizeof title, "%s", new_title);
        /* D-Bus refuses text that is not valid UTF-8, which a title cut
         * off in the middle of a character would be: drop the stub. */
        for (n = strlen(title); n && ((unsigned char)title[n - 1] & 0xC0) == 0x80; n--)
            ;
        if (n && (unsigned char)title[n - 1] >= 0xC0 && strlen(new_title) >= sizeof title)
            title[n - 1] = '\0';
    }
    if (bus && changed)
        announce();
}
