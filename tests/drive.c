/*
 * Drives a running Amplitude with synthetic X11 input and checks where its
 * windows end up. Used by gui_test.sh.
 *
 *     DRIVE_PID=<pid of the player> drive <command> [arguments] ...
 *
 * Only windows belonging to that process are ever touched: the player sets
 * _NET_WM_PID on its windows and everything here is matched against it.
 * Never run this against a player somebody is using.
 *
 * Windows are numbered 0 main, 1 equaliser, 2 playlist, 3 jump to file.
 * Coordinates are real pixels inside the window.
 *
 *   key W name mods          press a key (X keysym name; mods: 1 shift, 4 ctrl)
 *   click W x y mods         left click
 *   rclick W x y sx sy       right click; sx, sy is the screen position reported
 *   drag W x y dx dy         left-button drag by (dx, dy)
 *   menu x y                 click inside the open popup menu
 *   drop W uri-list          drag and drop (file:// URIs separated by \r\n)
 *   wait ms
 *
 * Checks (each prints a line; any failure makes the exit status 1):
 *   size W w h               window is visible with this size
 *   hidden W
 *   below A B                B sits directly under A, left edges aligned
 *   moved W dx dy            position relative to the last "mark W"
 *   mark W
 *   popup yes|no             a popup menu is / is not open
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *dpy;
static long player_pid;
static int failures;

static const char *const titles[] = { "Amplitude", "Amplitude Equalizer", "Amplitude Playlist", "Jump to file",
                                      "About Amplitude" };

static long window_pid(Window w)
{
    Atom type;
    int format;
    unsigned long count, left;
    unsigned char *data = NULL;
    long pid = -1;

    if (XGetWindowProperty(dpy, w, XInternAtom(dpy, "_NET_WM_PID", False), 0, 1, False, XA_CARDINAL,
                           &type, &format, &count, &left, &data) == Success && data) {
        if (count)
            pid = *(long *)data;
        XFree(data);
    }
    return pid;
}

/* Depth-first search for the player's window with a given title. */
static Window find_titled(Window w, const char *title)
{
    Window root, parent, *children = NULL, found = 0;
    unsigned count, i;
    char *name = NULL;

    if (XFetchName(dpy, w, &name) && name) {
        int match = strcmp(name, title) == 0 && window_pid(w) == player_pid;

        XFree(name);
        if (match)
            return w;
    }
    if (!XQueryTree(dpy, w, &root, &parent, &children, &count))
        return 0;
    for (i = 0; i < count && !found; i++)
        found = find_titled(children[i], title);
    if (children)
        XFree(children);
    return found;
}

/* Looked up afresh every time: the player recreates its windows when the
 * size setting changes. */
static Window window(int index)
{
    Window w = index >= 0 && index < 5 ? find_titled(DefaultRootWindow(dpy), titles[index]) : 0;

    if (!w) {
        printf("FAIL  window %d not found\n", index);
        exit(1);
    }
    return w;
}

/* The player's popup menu: an unmanaged, viewable window carrying its pid. */
static Window popup(void)
{
    Window root, parent, *children = NULL, found = 0;
    unsigned count;
    int i;

    XQueryTree(dpy, DefaultRootWindow(dpy), &root, &parent, &children, &count);
    for (i = (int)count - 1; i >= 0 && !found; i--) {
        XWindowAttributes attrs;

        if (XGetWindowAttributes(dpy, children[i], &attrs) && attrs.override_redirect &&
            attrs.map_state == IsViewable && window_pid(children[i]) == player_pid)
            found = children[i];
    }
    if (children)
        XFree(children);
    return found;
}

static void geometry(Window w, int *x, int *y, int *width, int *height, int *visible)
{
    XWindowAttributes attrs;
    Window child;

    XGetWindowAttributes(dpy, w, &attrs);
    XTranslateCoordinates(dpy, w, DefaultRootWindow(dpy), 0, 0, x, y, &child);
    *width = attrs.width;
    *height = attrs.height;
    *visible = attrs.map_state == IsViewable;
}

static void check(int ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
    fflush(stdout);
}

static void pause_ms(int ms)
{
    XFlush(dpy);
    usleep((useconds_t)ms * 1000);
}

static void send_key(Window w, const char *name, unsigned mods)
{
    XEvent ev;

    memset(&ev, 0, sizeof ev);
    ev.xkey.type = KeyPress;
    ev.xkey.display = dpy;
    ev.xkey.window = w;
    ev.xkey.root = DefaultRootWindow(dpy);
    ev.xkey.same_screen = True;
    ev.xkey.state = mods;
    ev.xkey.keycode = XKeysymToKeycode(dpy, XStringToKeysym(name));
    XSendEvent(dpy, w, False, KeyPressMask, &ev);
    pause_ms(300);
}

static void send_button(Window w, int type, int x, int y, int sx, int sy, unsigned button, unsigned mods)
{
    XEvent ev;

    memset(&ev, 0, sizeof ev);
    ev.xbutton.type = type;
    ev.xbutton.display = dpy;
    ev.xbutton.window = w;
    ev.xbutton.root = DefaultRootWindow(dpy);
    ev.xbutton.x = x;
    ev.xbutton.y = y;
    ev.xbutton.x_root = sx;
    ev.xbutton.y_root = sy;
    ev.xbutton.button = button;
    ev.xbutton.state = mods;
    ev.xbutton.same_screen = True;
    XSendEvent(dpy, w, False, type == ButtonPress ? ButtonPressMask : ButtonReleaseMask, &ev);
    pause_ms(120);
}

static void send_motion(Window w, int x, int y, int sx, int sy)
{
    XEvent ev;

    memset(&ev, 0, sizeof ev);
    ev.xmotion.type = MotionNotify;
    ev.xmotion.display = dpy;
    ev.xmotion.window = w;
    ev.xmotion.root = DefaultRootWindow(dpy);
    ev.xmotion.x = x;
    ev.xmotion.y = y;
    ev.xmotion.x_root = sx;
    ev.xmotion.y_root = sy;
    ev.xmotion.same_screen = True;
    XSendEvent(dpy, w, False, PointerMotionMask, &ev);
    pause_ms(120);
}

static void send_message(Window to, Window from, const char *type, long l1, long l2, long l3, long l4)
{
    XEvent ev;

    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.display = dpy;
    ev.xclient.window = to;
    ev.xclient.message_type = XInternAtom(dpy, type, False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)from;
    ev.xclient.data.l[1] = l1;
    ev.xclient.data.l[2] = l2;
    ev.xclient.data.l[3] = l3;
    ev.xclient.data.l[4] = l4;
    XSendEvent(dpy, to, False, NoEventMask, &ev);
    XFlush(dpy);
}

/* Plays the source side of the XDND protocol: announce, drop, then hand
 * over the URI list when the player asks for the selection. */
static void drop(Window target, const char *uris)
{
    Window source = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    Atom selection = XInternAtom(dpy, "XdndSelection", False), uri_list = XInternAtom(dpy, "text/uri-list", False);
    Atom finished = XInternAtom(dpy, "XdndFinished", False);
    int tries, done = 0;

    XSetSelectionOwner(dpy, selection, source, CurrentTime);
    send_message(target, source, "XdndEnter", 5L << 24, (long)uri_list, 0, 0);
    send_message(target, source, "XdndPosition", 0, (10 << 16) | 10, CurrentTime,
                 (long)XInternAtom(dpy, "XdndActionCopy", False));
    pause_ms(100);
    send_message(target, source, "XdndDrop", 0, CurrentTime, 0, 0);
    for (tries = 0; tries < 200 && !done; tries++) {
        while (XPending(dpy)) {
            XEvent ev, reply;

            XNextEvent(dpy, &ev);
            if (ev.type == SelectionRequest) {
                XChangeProperty(dpy, ev.xselectionrequest.requestor, ev.xselectionrequest.property, uri_list, 8,
                                PropModeReplace, (const unsigned char *)uris, (int)strlen(uris));
                memset(&reply, 0, sizeof reply);
                reply.xselection.type = SelectionNotify;
                reply.xselection.display = dpy;
                reply.xselection.requestor = ev.xselectionrequest.requestor;
                reply.xselection.selection = selection;
                reply.xselection.target = ev.xselectionrequest.target;
                reply.xselection.property = ev.xselectionrequest.property;
                reply.xselection.time = ev.xselectionrequest.time;
                XSendEvent(dpy, ev.xselectionrequest.requestor, False, NoEventMask, &reply);
                XFlush(dpy);
            } else if (ev.type == ClientMessage && ev.xclient.message_type == finished) {
                done = (int)(ev.xclient.data.l[1] & 1);
            }
        }
        usleep(10000);
    }
    check(done, "drop accepted by the player");
    XDestroyWindow(dpy, source);
    pause_ms(300);
}

int main(int argc, char **argv)
{
    static int mark_x[4], mark_y[4];
    char what[160];
    int i;

    if (!getenv("DRIVE_PID")) {
        fprintf(stderr, "drive: DRIVE_PID is not set; refusing to guess which player to drive\n");
        return 2;
    }
    player_pid = atol(getenv("DRIVE_PID"));
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "drive: cannot open X display\n");
        return 2;
    }

#define ARG(n) atoi(argv[i + (n)])
#define NEED(n) if (i + (n) >= argc) { fprintf(stderr, "drive: %s needs %d arguments\n", argv[i], (n)); return 2; }
    for (i = 1; i < argc; i++) {
        const char *cmd = argv[i];
        int x, y, w, h, visible;

        if (!strcmp(cmd, "key")) {
            NEED(3);
            send_key(window(ARG(1)), argv[i + 2], (unsigned)ARG(3));
            i += 3;
        } else if (!strcmp(cmd, "click")) {
            Window win;

            NEED(4);
            win = window(ARG(1));
            send_button(win, ButtonPress, ARG(2), ARG(3), 0, 0, 1, (unsigned)ARG(4));
            send_button(win, ButtonRelease, ARG(2), ARG(3), 0, 0, 1, (unsigned)ARG(4));
            pause_ms(450);      /* longer than the double-click interval */
            i += 4;
        } else if (!strcmp(cmd, "rclick")) {
            NEED(5);
            send_button(window(ARG(1)), ButtonRelease, ARG(2), ARG(3), ARG(4), ARG(5), 3, 0);
            pause_ms(300);
            i += 5;
        } else if (!strcmp(cmd, "drag")) {
            Window win;
            int dx, dy;

            NEED(5);
            win = window(ARG(1));
            x = ARG(2);
            y = ARG(3);
            dx = ARG(4);
            dy = ARG(5);
            send_button(win, ButtonPress, x, y, 1000, 1000, 1, 0);
            send_motion(win, x + dx / 2, y + dy / 2, 1000 + dx / 2, 1000 + dy / 2);
            send_motion(win, x + dx, y + dy, 1000 + dx, 1000 + dy);
            send_button(win, ButtonRelease, x + dx, y + dy, 1000 + dx, 1000 + dy, 1, 0);
            pause_ms(300);
            i += 5;
        } else if (!strcmp(cmd, "menu")) {
            Window menu = popup();

            NEED(2);
            check(menu != 0, "a menu is open to click in");
            if (menu) {
                send_motion(menu, ARG(1), ARG(2), 0, 0);
                send_button(menu, ButtonPress, ARG(1), ARG(2), 0, 0, 1, 0);
            }
            pause_ms(500);
            i += 2;
        } else if (!strcmp(cmd, "drop")) {
            NEED(2);
            drop(window(ARG(1)), argv[i + 2]);
            i += 2;
        } else if (!strcmp(cmd, "wait")) {
            NEED(1);
            pause_ms(ARG(1));
            i += 1;
        } else if (!strcmp(cmd, "size")) {
            NEED(3);
            geometry(window(ARG(1)), &x, &y, &w, &h, &visible);
            snprintf(what, sizeof what, "window %d is visible at %dx%d (found %s %dx%d)", ARG(1), ARG(2), ARG(3),
                     visible ? "visible" : "hidden", w, h);
            check(visible && w == ARG(2) && h == ARG(3), what);
            i += 3;
        } else if (!strcmp(cmd, "hidden")) {
            NEED(1);
            geometry(window(ARG(1)), &x, &y, &w, &h, &visible);
            snprintf(what, sizeof what, "window %d is hidden", ARG(1));
            check(!visible, what);
            i += 1;
        } else if (!strcmp(cmd, "below")) {
            int ax, ay, aw, ah, avisible;

            NEED(2);
            geometry(window(ARG(1)), &ax, &ay, &aw, &ah, &avisible);
            geometry(window(ARG(2)), &x, &y, &w, &h, &visible);
            snprintf(what, sizeof what, "window %d is docked under window %d (%d,%d vs %d,%d+%d)", ARG(2), ARG(1),
                     x, y, ax, ay, ah);
            check(visible && avisible && x == ax && y == ay + ah, what);
            i += 2;
        } else if (!strcmp(cmd, "mark")) {
            NEED(1);
            geometry(window(ARG(1)), &mark_x[ARG(1) & 3], &mark_y[ARG(1) & 3], &w, &h, &visible);
            i += 1;
        } else if (!strcmp(cmd, "moved")) {
            NEED(3);
            geometry(window(ARG(1)), &x, &y, &w, &h, &visible);
            snprintf(what, sizeof what, "window %d moved by %d,%d (found %d,%d)", ARG(1), ARG(2), ARG(3),
                     x - mark_x[ARG(1) & 3], y - mark_y[ARG(1) & 3]);
            check(x - mark_x[ARG(1) & 3] == ARG(2) && y - mark_y[ARG(1) & 3] == ARG(3), what);
            i += 3;
        } else if (!strcmp(cmd, "popup")) {
            int want;

            NEED(1);
            want = !strcmp(argv[i + 1], "yes");
            check((popup() != 0) == want, want ? "a popup menu is open" : "no popup menu is open");
            i += 1;
        } else {
            fprintf(stderr, "drive: unknown command %s\n", cmd);
            return 2;
        }
    }
    XCloseDisplay(dpy);
    return failures != 0;
}
