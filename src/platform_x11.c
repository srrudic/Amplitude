/* X11 backend. Also used on Wayland desktops through XWayland, because
 * native Wayland clients cannot position their own windows. */
#include "platform.h"

#include "icon.h"
#include "mpris.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <dirent.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct PlatWindow {
    Window xwin;
    GC gc;
    XImage *image;
    uint32_t *pixels;       /* copy of the last presented frame, owned by the XImage */
    int stale;              /* the screen no longer shows that frame: send all of it */
    int w, h, scale;        /* framebuffer size and magnification in percent */
    int rw, rh;             /* window size in real pixels */
    int x, y;               /* last requested or reported position */
    int mapped, owned, popup;
    XIC input;              /* text input context, NULL if unavailable */
    PlatWindow *next;
};

/* Files waiting to be reported as PEV_FILE events. */
typedef struct {
    char *path;
    int flags;
    PlatWindow *win;
} FileEvent;

static Display *dpy;
static XIM input_method;
static PlatEvent pending_text;  /* PEV_TEXT waiting to follow its key event */
static PlatWindow *windows;
static PlatWindow *main_window;     /* receives the media keys */

/* Media keys. Where the desktop has media controls (MPRIS, see mpris.c) it
 * owns the keys and passes them on, and that is the only route used, since
 * listening for the keys as well would act on each press twice. Without
 * one, the keys are grabbed from the X server directly. */
#define MEDIA_QUEUE 8
static int media_keys[MEDIA_QUEUE], media_count;
static int have_mpris;
static const struct { KeySym sym; int key; } media_syms[] = {      /* from XF86keysym.h */
    { 0x1008FF14, PK_MEDIA_PLAY_PAUSE },    /* XF86AudioPlay: the play/pause key */
    { 0x1008FF31, PK_MEDIA_PAUSE },         /* XF86AudioPause */
    { 0x1008FF15, PK_MEDIA_STOP },          /* XF86AudioStop */
    { 0x1008FF16, PK_MEDIA_PREV },          /* XF86AudioPrev */
    { 0x1008FF17, PK_MEDIA_NEXT },          /* XF86AudioNext */
};
#define MEDIA_SYMS ((int)(sizeof media_syms / sizeof media_syms[0]))
static Atom wm_protocols, wm_delete_window, net_wm_state, net_wm_skip_taskbar;
static Atom xdnd_aware, xdnd_enter, xdnd_position, xdnd_status, xdnd_drop, xdnd_finished;
static Atom xdnd_selection, xdnd_action_copy, xdnd_type_list, uri_list;
static Atom instance_selection, instance_command;

static FileEvent *files;
static int file_count, file_pos, file_capacity;
static char *last_path;         /* owned by the event last handed out */
static Window dnd_source;
static int dnd_accept;

int plat_init(void)
{
    char settings[1024] = "", name[64];
    unsigned long hash = 5381;
    const char *p;

    /* Typed text is decoded according to the user's locale and keyboard. */
    setlocale(LC_CTYPE, "");
    XSetLocaleModifiers("");
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "amplitude: cannot open X display\n");
        return 0;
    }
    input_method = XOpenIM(dpy, NULL, NULL, NULL);
    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_skip_taskbar = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    xdnd_aware = XInternAtom(dpy, "XdndAware", False);
    xdnd_enter = XInternAtom(dpy, "XdndEnter", False);
    xdnd_position = XInternAtom(dpy, "XdndPosition", False);
    xdnd_status = XInternAtom(dpy, "XdndStatus", False);
    xdnd_drop = XInternAtom(dpy, "XdndDrop", False);
    xdnd_finished = XInternAtom(dpy, "XdndFinished", False);
    xdnd_selection = XInternAtom(dpy, "XdndSelection", False);
    xdnd_action_copy = XInternAtom(dpy, "XdndActionCopy", False);
    xdnd_type_list = XInternAtom(dpy, "XdndTypeList", False);
    uri_list = XInternAtom(dpy, "text/uri-list", False);
    /* One player per settings directory: a second one started with different
     * settings (another user, or XDG_CONFIG_HOME pointed elsewhere) is a
     * separate player rather than a remote control for the first. */
    plat_config_dir(settings, sizeof settings);
    for (p = settings; *p; p++)
        hash = hash * 33 + (unsigned char)*p;
    snprintf(name, sizeof name, "_AMPLITUDE_INSTANCE_%08lX", hash & 0xFFFFFFFFul);
    instance_selection = XInternAtom(dpy, name, False);
    instance_command = XInternAtom(dpy, "_AMPLITUDE_COMMAND", False);
    return 1;
}

static int ignore_x_error(Display *display, XErrorEvent *error)
{
    (void)display;
    (void)error;
    return 0;
}

/* Asks the X server for the media keys wherever the keyboard is. Another
 * program may hold them already, which is reported as an error and simply
 * means we do not get them. */
static void grab_media_keys(void)
{
    static const unsigned locks[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };    /* Caps Lock, Num Lock */
    int (*previous)(Display *, XErrorEvent *) = XSetErrorHandler(ignore_x_error);
    int i;
    size_t k;

    for (i = 0; i < MEDIA_SYMS; i++) {
        KeyCode code = XKeysymToKeycode(dpy, media_syms[i].sym);

        for (k = 0; code && k < sizeof locks / sizeof locks[0]; k++)
            XGrabKey(dpy, code, locks[k], DefaultRootWindow(dpy), False, GrabModeAsync, GrabModeAsync);
    }
    XSync(dpy, False);
    XSetErrorHandler(previous);
}

static void push_media_key(int key)
{
    if (media_count < MEDIA_QUEUE)
        media_keys[media_count++] = key;
}

static void on_mpris_command(int command)
{
    switch (command) {
    case MPRIS_PLAY:       push_media_key(PK_MEDIA_PLAY); break;
    case MPRIS_PAUSE:      push_media_key(PK_MEDIA_PAUSE); break;
    case MPRIS_PLAY_PAUSE: push_media_key(PK_MEDIA_PLAY_PAUSE); break;
    case MPRIS_STOP:       push_media_key(PK_MEDIA_STOP); break;
    case MPRIS_NEXT:       push_media_key(PK_MEDIA_NEXT); break;
    case MPRIS_PREVIOUS:   push_media_key(PK_MEDIA_PREV); break;
    case MPRIS_RAISE:
        if (main_window)
            XRaiseWindow(dpy, main_window->xwin);
        break;
    case MPRIS_QUIT:
        push_media_key(-1);     /* stands for PEV_QUIT, see plat_poll_event() */
        break;
    }
}

void plat_media_update(int state, const char *title, double position, double length)
{
    if (have_mpris)
        mpris_update(state, title, position, length);
}

void plat_shutdown(void)
{
    if (have_mpris)
        mpris_shutdown();
    have_mpris = 0;
    main_window = NULL;
    while (windows)
        plat_window_destroy(windows);
    if (input_method)
        XCloseIM(input_method);
    input_method = NULL;
    if (dpy)
        XCloseDisplay(dpy);
    dpy = NULL;
    for (; file_pos < file_count; file_pos++)
        free(files[file_pos].path);
    free(files);
    free(last_path);
    files = NULL;
    last_path = NULL;
    file_count = file_pos = file_capacity = 0;
}

/* --- Windows ---------------------------------------------------------------- */

/* (Re)creates the image buffer for the window's current size. */
static void make_image(PlatWindow *win)
{
    int screen = DefaultScreen(dpy);

    if (win->image) {
        win->image->data = NULL;    /* owned by us, not by XDestroyImage */
        XDestroyImage(win->image);
    }
    free(win->pixels);
    win->rw = PLAT_SCALED(win->w, win->scale);
    win->rh = PLAT_SCALED(win->h, win->scale);
    win->pixels = malloc(sizeof(uint32_t) * win->rw * win->rh);
    win->stale = 1;
    win->image = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen), ZPixmap, 0,
                              (char *)win->pixels, win->rw, win->rh, 32, 0);
}

/* Window managers read these when the window is mapped or resized. */
static void set_size_hints(PlatWindow *win)
{
    XSizeHints *size = XAllocSizeHints();

    size->flags = PMinSize | PMaxSize | PPosition | USPosition;
    size->x = win->x;
    size->y = win->y;
    size->min_width = size->max_width = win->rw;
    size->min_height = size->max_height = win->rh;
    XSetWMNormalHints(dpy, win->xwin, size);
    XFree(size);
}

/* _NET_WM_ICON: width, height, then ARGB pixels, one per long. */
static void set_icon(PlatWindow *win)
{
    static unsigned long data[2 + APP_ICON_SIZE * APP_ICON_SIZE];
    int i;

    data[0] = data[1] = APP_ICON_SIZE;
    for (i = 0; i < APP_ICON_SIZE * APP_ICON_SIZE; i++)
        data[2 + i] = app_icon[i];
    XChangeProperty(dpy, win->xwin, XInternAtom(dpy, "_NET_WM_ICON", False), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)data, 2 + APP_ICON_SIZE * APP_ICON_SIZE);
}

static PlatWindow *create_window(const char *title, int w, int h, int scale, int x, int y,
                                 PlatWindow *owner, int popup)
{
    /* _MOTIF_WM_HINTS with decorations = 0 asks the window manager for a
     * borderless window while keeping it managed (taskbar, focus, etc.). */
    struct { unsigned long flags, functions, decorations; long input_mode; unsigned long status; }
        motif = { 2, 0, 0, 0, 0 };
    int screen = DefaultScreen(dpy);
    long version = 5, pid = (long)getpid();
    XClassHint *cls;
    Atom motif_atom;
    PlatWindow *win = calloc(1, sizeof *win);

    if (!win)
        return NULL;
    win->w = w;
    win->h = h;
    win->scale = scale;
    win->x = x;
    win->y = y;
    win->owned = owner != NULL;
    win->popup = popup;
    win->xwin = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), x, y, PLAT_SCALED(w, scale),
                                    PLAT_SCALED(h, scale), 0, 0, 0);
    win->gc = XCreateGC(dpy, win->xwin, 0, NULL);
    make_image(win);

    XSelectInput(dpy, win->xwin, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                                 KeyPressMask | StructureNotifyMask | PropertyChangeMask);
    win->next = windows;
    windows = win;
    XChangeProperty(dpy, win->xwin, XInternAtom(dpy, "_NET_WM_PID", False), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&pid, 1);

    if (popup) {
        /* Override-redirect windows bypass the window manager entirely. */
        XSetWindowAttributes attrs;

        attrs.override_redirect = True;
        XChangeWindowAttributes(dpy, win->xwin, CWOverrideRedirect, &attrs);
        XMapRaised(dpy, win->xwin);
        XGrabPointer(dpy, win->xwin, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        win->mapped = 1;
        return win;
    }

    if (input_method)
        win->input = XCreateIC(input_method, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                               XNClientWindow, win->xwin, XNFocusWindow, win->xwin, NULL);
    XStoreName(dpy, win->xwin, title);
    XSetWMProtocols(dpy, win->xwin, &wm_delete_window, 1);
    motif_atom = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    XChangeProperty(dpy, win->xwin, motif_atom, motif_atom, 32, PropModeReplace,
                    (unsigned char *)&motif, 5);
    XChangeProperty(dpy, win->xwin, xdnd_aware, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&version, 1);
    if (owner)
        XSetTransientForHint(dpy, win->xwin, owner->xwin);
    set_icon(win);

    cls = XAllocClassHint();
    cls->res_name = cls->res_class = (char *)"amplitude";
    XSetClassHint(dpy, win->xwin, cls);
    XFree(cls);
    return win;
}

PlatWindow *plat_window_create(const char *title, int w, int h, int scale, PlatWindow *owner)
{
    return create_window(title, w, h, scale, 100, 100, owner, 0);
}

PlatWindow *plat_popup_create(int w, int h, int scale, int sx, int sy, PlatWindow *owner)
{
    return create_window("", w, h, scale, sx, sy, owner, 1);
}

void plat_window_destroy(PlatWindow *win)
{
    PlatWindow **p;

    for (p = &windows; *p; p = &(*p)->next) {
        if (*p == win) {
            *p = win->next;
            break;
        }
    }
    if (win == main_window)
        main_window = NULL;
    if (win->popup)
        XUngrabPointer(dpy, CurrentTime);
    if (win->input)
        XDestroyIC(win->input);
    win->image->data = NULL;    /* owned by us, not by XDestroyImage */
    XDestroyImage(win->image);
    XFreeGC(dpy, win->gc);
    XDestroyWindow(dpy, win->xwin);
    XFlush(dpy);
    free(win->pixels);
    free(win);
}

void plat_window_show(PlatWindow *win, int visible)
{
    if (visible == win->mapped)
        return;
    win->mapped = visible;
    if (!visible) {
        XWithdrawWindow(dpy, win->xwin, DefaultScreen(dpy));
        return;
    }
    set_size_hints(win);
    if (win->owned)
        XChangeProperty(dpy, win->xwin, net_wm_state, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&net_wm_skip_taskbar, 1);
    XMapWindow(dpy, win->xwin);
    XMoveWindow(dpy, win->xwin, win->x, win->y);
}

void plat_window_resize(PlatWindow *win, int w, int h)
{
    win->w = w;
    win->h = h;
    make_image(win);
    set_size_hints(win);
    XResizeWindow(dpy, win->xwin, win->rw, win->rh);
}

void plat_window_present(PlatWindow *win, const uint32_t *pixels)
{
    int top = 0, bottom = win->rh;

    if (!win->stale)
        plat_changed_rows(win->pixels, pixels, win->rw, win->rh, &top, &bottom);
    if (top == bottom)
        return;
    memcpy(win->pixels + (size_t)top * win->rw, pixels + (size_t)top * win->rw,
           sizeof(uint32_t) * win->rw * (size_t)(bottom - top));
    XPutImage(dpy, win->xwin, win->gc, win->image, 0, top, 0, top, win->rw, bottom - top);
    XFlush(dpy);
    win->stale = 0;
}

void plat_window_get_pos(PlatWindow *win, int *sx, int *sy)
{
    /* Asking the server directly would race with the window manager, which
     * applies moves asynchronously; ConfigureNotify keeps these up to date. */
    *sx = win->x;
    *sy = win->y;
}

void plat_window_set_pos(PlatWindow *win, int sx, int sy)
{
    win->x = sx;
    win->y = sy;
    if (win->mapped)
        XMoveWindow(dpy, win->xwin, sx, sy);
}

void plat_window_minimize(PlatWindow *win)
{
    XIconifyWindow(dpy, win->xwin, DefaultScreen(dpy));
}

void plat_window_focus(PlatWindow *win)
{
    /* Ask the window manager rather than grabbing focus ourselves, which
     * is an error while the window is still being mapped. */
    XEvent request;

    memset(&request, 0, sizeof request);
    request.xclient.type = ClientMessage;
    request.xclient.window = win->xwin;
    request.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    request.xclient.format = 32;
    request.xclient.data.l[0] = 1;      /* from an application */
    XSendEvent(dpy, DefaultRootWindow(dpy), False, SubstructureNotifyMask | SubstructureRedirectMask,
               &request);
    XRaiseWindow(dpy, win->xwin);
}

static PlatWindow *find_window(Window xwin)
{
    PlatWindow *win;

    for (win = windows; win; win = win->next)
        if (win->xwin == xwin)
            return win;
    return NULL;
}

/* --- File events: drag and drop, and files sent by another instance --------- */

static void push_file(const char *path, size_t len, int flags, PlatWindow *win)
{
    char *copy = malloc(len + 1);

    if (file_count == file_capacity) {
        int capacity = file_capacity ? file_capacity * 2 : 16;
        FileEvent *grown = realloc(files, sizeof *files * capacity);

        if (!grown) {
            free(copy);
            return;
        }
        files = grown;
        file_capacity = capacity;
    }
    if (!copy)
        return;
    memcpy(copy, path, len);
    copy[len] = '\0';
    files[file_count].path = copy;
    files[file_count].flags = flags;
    files[file_count].win = win;
    file_count++;
}

static int hex_digit(int ch)
{
    return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
           ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
}

/* text/uri-list: one "file://host/path" per line, percent-encoded. */
static void push_uri_list(char *text, PlatWindow *win)
{
    char *line = text, *end;
    int first = 1;

    for (; *line; line = end) {
        char *src, *dst;

        end = line + strcspn(line, "\r\n");
        while (*end == '\r' || *end == '\n')
            *end++ = '\0';
        if (strncmp(line, "file://", 7) != 0)
            continue;
        src = strchr(line + 7, '/');    /* skip the optional host name */
        if (!src)
            continue;
        for (dst = line; *src; src++) {
            if (src[0] == '%' && hex_digit(src[1]) >= 0 && hex_digit(src[2]) >= 0) {
                *dst++ = (char)(hex_digit(src[1]) * 16 + hex_digit(src[2]));
                src += 2;
            } else {
                *dst++ = *src;
            }
        }
        push_file(line, (size_t)(dst - line), first ? PFILE_FIRST : 0, win);
        first = 0;
    }
}

static void send_dnd_message(PlatWindow *win, Atom type, long l1, long l4)
{
    XEvent reply;

    memset(&reply, 0, sizeof reply);
    reply.xclient.type = ClientMessage;
    reply.xclient.display = dpy;
    reply.xclient.window = dnd_source;
    reply.xclient.message_type = type;
    reply.xclient.format = 32;
    reply.xclient.data.l[0] = (long)win->xwin;
    reply.xclient.data.l[1] = l1;
    reply.xclient.data.l[type == xdnd_finished ? 2 : 4] = l4;
    XSendEvent(dpy, dnd_source, False, NoEventMask, &reply);
    XFlush(dpy);
}

/* Reads a whole window property as bytes (NUL-terminated). Caller XFree()s. */
static unsigned char *read_property(Window xwin, Atom property, int delete, unsigned long *count,
                                    Atom *type, int *format)
{
    unsigned char *data = NULL;
    unsigned long remaining;

    if (XGetWindowProperty(dpy, xwin, property, 0, 1 << 22, delete, AnyPropertyType, type, format,
                           count, &remaining, &data) != Success)
        return NULL;
    return data;
}

static void handle_dnd(PlatWindow *win, XClientMessageEvent *msg)
{
    if (msg->message_type == xdnd_enter) {
        unsigned long count = 3, i;
        Atom *types = (Atom *)&msg->data.l[2], type;
        unsigned char *list = NULL;
        int format;

        dnd_source = (Window)msg->data.l[0];
        dnd_accept = 0;
        if (msg->data.l[1] & 1) {       /* more than three types: ask the source */
            list = read_property(dnd_source, xdnd_type_list, False, &count, &type, &format);
            types = (Atom *)list;
        }
        for (i = 0; types && i < count; i++)
            if (types[i] == uri_list)
                dnd_accept = 1;
        if (list)
            XFree(list);
    } else if (msg->message_type == xdnd_position) {
        send_dnd_message(win, xdnd_status, dnd_accept, dnd_accept ? (long)xdnd_action_copy : 0);
    } else if (msg->message_type == xdnd_drop) {
        if (dnd_accept)
            XConvertSelection(dpy, xdnd_selection, uri_list, xdnd_selection, win->xwin,
                              (Time)msg->data.l[2]);
        else
            send_dnd_message(win, xdnd_finished, 0, 0);
    }
}

/* Another instance appended "<flags byte><path>\0" records to our window. */
static void handle_instance_command(PlatWindow *win)
{
    unsigned long count, pos = 0;
    Atom type;
    int format, first = 1;
    unsigned char *data = read_property(win->xwin, instance_command, True, &count, &type, &format);

    if (!data)
        return;
    while (pos < count) {
        size_t len = strlen((char *)data + pos + 1);

        if (len)
            push_file((char *)data + pos + 1, len,
                      PFILE_REMOTE | (first ? PFILE_FIRST : 0) | (data[pos] == 'E' ? PFILE_ENQUEUE : 0), win);
        first = 0;
        pos += len + 2;
    }
    XFree(data);
    XRaiseWindow(dpy, win->xwin);
}

int plat_instance_send(const char *const *paths, int count, int enqueue)
{
    Window owner = XGetSelectionOwner(dpy, instance_selection);
    int i;

    if (owner == None)
        return 0;
    if (!count)     /* nothing to play: an empty record just raises the window */
        XChangeProperty(dpy, owner, instance_command, XA_STRING, 8, PropModeAppend,
                        (const unsigned char *)"P", 2);
    for (i = 0; i < count; i++) {
        size_t len = strlen(paths[i]);
        char *record = malloc(len + 2);

        if (!record)
            break;
        record[0] = enqueue ? 'E' : 'P';
        memcpy(record + 1, paths[i], len + 1);
        XChangeProperty(dpy, owner, instance_command, XA_STRING, 8, PropModeAppend,
                        (unsigned char *)record, (int)len + 2);
        free(record);
    }
    XFlush(dpy);
    return 1;
}

void plat_instance_claim(PlatWindow *window)
{
    static int media_ready;

    XSetSelectionOwner(dpy, instance_selection, window->xwin, CurrentTime);
    /* Being the player is also when the media keys become ours. This runs
     * again when the windows are rebuilt at another size; once is enough. */
    main_window = window;
    if (!media_ready) {
        media_ready = 1;
        have_mpris = mpris_init();
        if (!have_mpris)
            grab_media_keys();
    }
}

/* --- Events ----------------------------------------------------------------- */

static int translate_key(XKeyEvent *xkey)
{
    KeySym sym = XLookupKeysym(xkey, 0);

    switch (sym) {
    case XK_Left:   return PK_LEFT;
    case XK_Right:  return PK_RIGHT;
    case XK_Up:     return PK_UP;
    case XK_Down:   return PK_DOWN;
    case XK_Escape: return PK_ESCAPE;
    case XK_Return:
    case XK_KP_Enter: return PK_ENTER;
    case XK_Delete: return PK_DELETE;
    case XK_BackSpace: return PK_BACKSPACE;
    }
    if (sym >= XK_a && sym <= XK_z)
        return (int)(sym - XK_a) + 'A';
    if (sym >= XK_space && sym <= XK_asciitilde)
        return (int)sym;
    return 0;
}

/* Works out what character, if any, a key press types, and queues it as a
 * PEV_TEXT event to follow the key event. */
static void queue_text(PlatWindow *win, XKeyEvent *xkey)
{
    char buffer[16];
    KeySym sym;
    Status status;
    int len;

    if (xkey->state & (ControlMask | Mod1Mask))
        return;
    if (win->input) {
        len = Xutf8LookupString(win->input, xkey, buffer, sizeof buffer - 1, &sym, &status);
        if (status != XLookupChars && status != XLookupBoth)
            return;
    } else {
        /* No input method: Latin-1 only, converted to UTF-8 by hand. */
        unsigned char latin1;

        len = XLookupString(xkey, buffer, 1, &sym, NULL);
        latin1 = (unsigned char)buffer[0];
        if (len == 1 && latin1 >= 0x80) {
            buffer[0] = (char)(0xC0 | latin1 >> 6);
            buffer[1] = (char)(0x80 | (latin1 & 0x3F));
            len = 2;
        }
    }
    if (len <= 0 || len >= (int)sizeof pending_text.text || (unsigned char)buffer[0] < 0x20 || buffer[0] == 0x7F)
        return;
    memset(&pending_text, 0, sizeof pending_text);
    pending_text.type = PEV_TEXT;
    pending_text.win = win;
    memcpy(pending_text.text, buffer, (size_t)len);
}

static int translate_mods(unsigned state)
{
    return (state & ShiftMask ? PMOD_SHIFT : 0) | (state & ControlMask ? PMOD_CTRL : 0);
}

int plat_poll_event(PlatEvent *ev)
{
    for (;;) {
        XEvent xev;

        if (pending_text.type) {
            *ev = pending_text;
            pending_text.type = PEV_NONE;
            return 1;
        }
        /* What the desktop's media controls asked for */
        if (have_mpris && !media_count)
            mpris_poll(on_mpris_command);
        if (media_count && main_window) {
            int key = media_keys[0];

            memmove(media_keys, media_keys + 1, sizeof media_keys[0] * (size_t)--media_count);
            memset(ev, 0, sizeof *ev);
            ev->win = main_window;
            ev->type = key < 0 ? PEV_QUIT : PEV_KEY_DOWN;
            ev->key = key < 0 ? 0 : key;
            return 1;
        }
        /* Queued files first, one per call. */
        if (file_pos < file_count) {
            memset(ev, 0, sizeof *ev);
            free(last_path);
            last_path = files[file_pos].path;
            ev->type = PEV_FILE;
            ev->win = files[file_pos].win;
            ev->flags = files[file_pos].flags;
            ev->path = last_path;
            if (++file_pos == file_count)
                file_pos = file_count = 0;
            return 1;
        }
        if (!XPending(dpy))
            return 0;

        XNextEvent(dpy, &xev);
        if (XFilterEvent(&xev, None))
            continue;       /* consumed by the input method */
        memset(ev, 0, sizeof *ev);
        /* A grabbed media key is reported on the root window. */
        if (xev.type == KeyPress && xev.xany.window == DefaultRootWindow(dpy)) {
            KeySym sym = XLookupKeysym(&xev.xkey, 0);
            int i;

            for (i = 0; i < MEDIA_SYMS; i++)
                if (media_syms[i].sym == sym)
                    push_media_key(media_syms[i].key);
            continue;
        }
        ev->win = find_window(xev.xany.window);
        if (!ev->win)
            continue;

        switch (xev.type) {
        case ConfigureNotify:
            /* Only synthetic events carry root coordinates; real ones are
             * relative to the window manager's frame. */
            if (xev.xconfigure.send_event) {
                ev->win->x = xev.xconfigure.x;
                ev->win->y = xev.xconfigure.y;
            } else {
                Window child;

                XTranslateCoordinates(dpy, ev->win->xwin, DefaultRootWindow(dpy), 0, 0,
                                      &ev->win->x, &ev->win->y, &child);
            }
            break;
        case ClientMessage:
            if (xev.xclient.message_type == wm_protocols &&
                (Atom)xev.xclient.data.l[0] == wm_delete_window) {
                ev->type = PEV_QUIT;
                return 1;
            }
            handle_dnd(ev->win, &xev.xclient);
            break;
        case SelectionNotify:
            if (xev.xselection.property != None) {
                unsigned long count;
                Atom type;
                int format;
                unsigned char *data = read_property(ev->win->xwin, xev.xselection.property, True,
                                                    &count, &type, &format);

                if (data) {
                    push_uri_list((char *)data, ev->win);
                    XFree(data);
                }
            }
            send_dnd_message(ev->win, xdnd_finished, 1, (long)xdnd_action_copy);
            break;
        case PropertyNotify:
            if (xev.xproperty.atom == instance_command && xev.xproperty.state == PropertyNewValue)
                handle_instance_command(ev->win);
            break;
        case Expose:
            ev->win->stale = 1;     /* the server discarded what was there */
            if (xev.xexpose.count == 0) {
                ev->type = PEV_EXPOSE;
                return 1;
            }
            break;
        case ButtonPress:
        case ButtonRelease:
            ev->mods = translate_mods(xev.xbutton.state);
            if (xev.xbutton.button > 3) {
                /* Buttons 4 and 5 are the scroll wheel. */
                if (xev.type != ButtonPress || xev.xbutton.button > 5)
                    break;
                ev->type = PEV_WHEEL;
                ev->wheel = xev.xbutton.button == 4 ? 1 : -1;
                return 1;
            }
            ev->type = xev.type == ButtonPress ? PEV_MOUSE_DOWN : PEV_MOUSE_UP;
            ev->button = xev.xbutton.button;
            /* Coordinates can be negative while a popup holds the grab. */
            ev->x = xev.xbutton.x < 0 ? -1 : xev.xbutton.x * 100 / ev->win->scale;
            ev->y = xev.xbutton.y < 0 ? -1 : xev.xbutton.y * 100 / ev->win->scale;
            ev->sx = xev.xbutton.x_root;
            ev->sy = xev.xbutton.y_root;
            return 1;
        case MotionNotify:
            /* Only the most recent position matters. */
            while (XCheckTypedWindowEvent(dpy, xev.xmotion.window, MotionNotify, &xev))
                ;
            ev->type = PEV_MOUSE_MOVE;
            ev->mods = translate_mods(xev.xmotion.state);
            ev->x = xev.xmotion.x < 0 ? -1 : xev.xmotion.x * 100 / ev->win->scale;
            ev->y = xev.xmotion.y < 0 ? -1 : xev.xmotion.y * 100 / ev->win->scale;
            ev->sx = xev.xmotion.x_root;
            ev->sy = xev.xmotion.y_root;
            return 1;
        case KeyPress:
            ev->key = translate_key(&xev.xkey);
            ev->mods = translate_mods(xev.xkey.state);
            queue_text(ev->win, &xev.xkey);
            if (ev->key) {
                ev->type = PEV_KEY_DOWN;
                return 1;
            }
            break;
        }
    }
}

void plat_wait(int timeout_ms)
{
    struct pollfd fds[2];
    int count = 1;

    XFlush(dpy);
    if (XPending(dpy) || file_pos < file_count || pending_text.type || media_count)
        return;
    fds[0].fd = ConnectionNumber(dpy);
    fds[0].events = POLLIN;
    if (have_mpris && mpris_fd() >= 0) {    /* a media key must not wait for the next frame */
        fds[1].fd = mpris_fd();
        fds[1].events = POLLIN;
        count = 2;
    }
    poll(fds, (nfds_t)count, timeout_ms);
}

uint32_t plat_ticks_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* --- Dialogs and the file system -------------------------------------------- */

/* X11 has no native dialogs, so borrow the desktop's helper tool. Each
 * command prints one chosen path per line. Returns the number of lines. */
static int run_dialog(const char *const *commands, int command_count, PlatPathFn fn, void *user)
{
    char line[PATH_MAX + 2];
    int i, count = 0;

    for (i = 0; i < command_count && !count; i++) {
        FILE *pipe = popen(commands[i], "r");

        if (!pipe)
            continue;
        while (fgets(line, sizeof line, pipe)) {
            size_t len = strlen(line);

            while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
            if (len) {
                fn(line, user);
                count++;
            }
        }
        /* A helper that ran but was cancelled must not trigger the next one. */
        if (pclose(pipe) == 256)
            break;
    }
    return count;
}

int plat_open_files_dialog(PlatPathFn fn, void *user)
{
    static const char *const commands[] = {
        "zenity --file-selection --multiple --separator='\n' --title='Add files' 2>/dev/null",
        "kdialog --getopenfilename . --multiple --separate-output 2>/dev/null",
    };

    return run_dialog(commands, 2, fn, user);
}

typedef struct {
    char *out;
    size_t size;
} PathBuffer;

static void copy_path(const char *path, void *user)
{
    PathBuffer *buffer = user;

    snprintf(buffer->out, buffer->size, "%s", path);
}

int plat_open_folder_dialog(char *out, size_t out_size)
{
    static const char *const commands[] = {
        "zenity --file-selection --directory --title='Add folder' 2>/dev/null",
        "kdialog --getexistingdirectory . 2>/dev/null",
    };
    PathBuffer buffer = { out, out_size };

    return run_dialog(commands, 2, copy_path, &buffer) > 0;
}

void plat_open_url(const char *url)
{
    pid_t child = fork();

    /* Forked twice, so the browser launcher is not our child and never
     * needs to be waited for. */
    if (child == 0) {
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", url, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    if (child > 0)
        waitpid(child, NULL, 0);
}

int plat_save_file_dialog(char *out, size_t out_size)
{
    static const char *const commands[] = {
        "zenity --file-selection --save --confirm-overwrite --title='Save playlist' --filename=playlist.m3u"
        " --file-filter='Playlists (*.m3u) | *.m3u' --file-filter='All files | *' 2>/dev/null",
        "kdialog --title 'Save playlist' --getsavefilename ./playlist.m3u 'Playlists (*.m3u);;All files (*)' 2>/dev/null",
    };
    PathBuffer buffer = { out, out_size };

    return run_dialog(commands, 2, copy_path, &buffer) > 0;
}

int plat_is_dir(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int plat_list_dir(const char *path, PlatPathFn fn, void *user)
{
    char full[PATH_MAX];
    DIR *dir = opendir(path);
    struct dirent *entry;

    if (!dir)
        return 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if ((size_t)snprintf(full, sizeof full, "%s/%s", path, entry->d_name) < sizeof full)
            fn(full, user);
    }
    closedir(dir);
    return 1;
}

/* Desktops publish their scaling to X clients as the Xft.dpi resource
 * (96 = 100%). Under XWayland it stays at 96 when the compositor scales
 * legacy windows itself, which is exactly when we should not. */
int plat_default_scale(void)
{
    const char *resources = XResourceManagerString(dpy);
    const char *entry = resources ? strstr(resources, "Xft.dpi:") : NULL;
    const char *gdk_scale = getenv("GDK_SCALE");
    double dpi = entry ? atof(entry + 8) : gdk_scale ? 96.0 * atof(gdk_scale) : 96.0;
    int percent = (int)(dpi * 100.0 / 96.0 + 0.5);

    return percent < 100 ? 100 : percent > 400 ? 400 : percent;
}

/* Plain Xlib only knows the whole desktop, across all monitors. Where the
 * window manager publishes the area left free by panels and docks, that is
 * used, so windows are kept clear of them too. */
void plat_screen_rect(int sx, int sy, int *x, int *y, int *w, int *h)
{
    Window root = DefaultRootWindow(dpy);
    unsigned long count = 0;
    Atom type;
    int format;
    unsigned char *data = read_property(root, XInternAtom(dpy, "_NET_WORKAREA", False), False, &count, &type, &format);

    (void)sx;
    (void)sy;
    *x = *y = 0;
    *w = DisplayWidth(dpy, DefaultScreen(dpy));
    *h = DisplayHeight(dpy, DefaultScreen(dpy));
    /* x, y, width, height for each virtual desktop; the first will do.
     * 32-bit properties arrive as an array of long. */
    if (data && format == 32 && count >= 4) {
        const long *area = (const long *)data;

        if (area[2] > 0 && area[3] > 0) {
            *x = (int)area[0];
            *y = (int)area[1];
            *w = (int)area[2];
            *h = (int)area[3];
        }
    }
    if (data)
        XFree(data);
}

/* $XDG_CONFIG_HOME/amplitude/, falling back to ~/.config/amplitude/. */
int plat_config_dir(char *out, size_t out_size)
{
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    int len;

    if (xdg && *xdg) {
        len = snprintf(out, out_size, "%s", xdg);
    } else if (home && *home) {
        len = snprintf(out, out_size, "%s/.config", home);
    } else {
        return 0;
    }
    if (len < 0 || (size_t)len + sizeof "/amplitude/" > out_size)
        return 0;
    mkdir(out, 0700);
    strcat(out, "/amplitude");
    mkdir(out, 0700);
    strcat(out, "/");
    return 1;
}

int plat_absolute_path(const char *path, char *out, size_t out_size)
{
    char resolved[PATH_MAX];

    if (!realpath(path, resolved) || strlen(resolved) >= out_size)
        return 0;
    strcpy(out, resolved);
    return 1;
}

/* File names are UTF-8 on any current Linux system, so there is nothing to
 * convert. */
FILE *plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

void plat_args(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
}
