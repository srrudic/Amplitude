/* Win32 backend. One binary serves both families of Windows:
 *
 * - On NT-based systems (NT4, 2000, XP and everything since) file names go
 *   through the wide-character ("W") functions, so any Unicode name works.
 * - On Windows 9x, where those functions are only stubs, the ANSI ("A")
 *   functions are used and names are limited to the system code page.
 *
 * Either way the rest of the program sees UTF-8. Wide functions outside
 * kernel32 are looked up at run time, so the executable still loads on
 * systems that lack them. Windows and messages use the ANSI functions
 * throughout, which exist everywhere. */
#include "platform.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CLASS_NAME "AmplitudeWindow"
#define MAIN_TITLE "Amplitude"
#define INSTANCE_MUTEX   "AmplitudeInstance"
#define INSTANCE_WAIT_MS 3000   /* how long to wait for a player that is still starting */
#define INSTANCE_POLL_MS 50
#define QUEUE_SIZE 64

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif

struct PlatWindow {
    HWND hwnd;
    int w, h, scale;        /* framebuffer size and magnification in percent */
    int rw, rh;             /* window size in real pixels */
    int owned, popup;
    uint32_t *shown;        /* copy of the last presented frame; NULL before the first */
    int shown_w, shown_h;   /* its size, to notice a resize */
    int stale;              /* the screen no longer shows that frame: send all of it */
};

/* Files waiting to be reported as PEV_FILE events. */
typedef struct {
    char *path;
    int flags;
    PlatWindow *win;
} FileEvent;

static HANDLE instance_mutex;      /* see plat_instance_send() */
static PlatEvent queue[QUEUE_SIZE];
static int queue_head, queue_tail;
static FileEvent *files;
static int file_count, file_pos, file_capacity;
static char *last_path;         /* owned by the event last handed out */

/* --- Text encodings --------------------------------------------------------- */

#define PATH_CHARS 1024     /* longest path handled, in UTF-16 units */

static int unicode_os(void)
{
    return (GetVersion() & 0x80000000) == 0;
}

/* Looks up a function that may be missing on old systems. The libraries
 * asked for are ones the program links against, so they are already loaded
 * and only need finding; loading is the fallback. */
static FARPROC optional(const char *dll, const char *name)
{
    HMODULE module = GetModuleHandleA(dll);

    if (!module)
        module = LoadLibraryA(dll);

    return module ? GetProcAddress(module, name) : NULL;
}

/* UTF-8 to UTF-16, by hand: the system's own UTF-8 conversion is missing on
 * Windows 95. `max` counts WCHARs including the terminator. */
static void utf8_to_wide(const char *text, WCHAR *out, int max)
{
    const unsigned char *p = (const unsigned char *)text;
    int n = 0;

    while (*p && n + 2 < max) {
        int extra = *p >= 0xF0 ? 3 : *p >= 0xE0 ? 2 : *p >= 0xC0 ? 1 : 0;
        unsigned long cp = extra == 3 ? *p & 0x07 : extra == 2 ? *p & 0x0F : extra ? *p & 0x1F : *p;

        for (p++; extra && (*p & 0xC0) == 0x80; extra--, p++)
            cp = cp << 6 | (*p & 0x3F);
        if (cp >= 0x10000) {            /* outside the basic plane: a surrogate pair */
            cp -= 0x10000;
            out[n++] = (WCHAR)(0xD800 | cp >> 10);
            out[n++] = (WCHAR)(0xDC00 | (cp & 0x3FF));
        } else {
            out[n++] = (WCHAR)cp;
        }
    }
    out[n] = 0;
}

static void wide_to_utf8(const WCHAR *text, char *out, size_t out_size)
{
    size_t n = 0;

    if (!out_size)
        return;
    while (*text && n + 4 < out_size) {
        unsigned long cp = *text++;

        if (cp >= 0xD800 && cp <= 0xDBFF && *text >= 0xDC00 && *text <= 0xDFFF)
            cp = 0x10000 + ((cp - 0xD800) << 10) + (*text++ - 0xDC00);
        if (cp < 0x80) {
            out[n++] = (char)cp;
        } else if (cp < 0x800) {
            out[n++] = (char)(0xC0 | cp >> 6);
            out[n++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out[n++] = (char)(0xE0 | cp >> 12);
            out[n++] = (char)(0x80 | (cp >> 6 & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[n++] = (char)(0xF0 | cp >> 18);
            out[n++] = (char)(0x80 | (cp >> 12 & 0x3F));
            out[n++] = (char)(0x80 | (cp >> 6 & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[n] = '\0';
}

/* UTF-8 to and from the system's legacy code page, for the ANSI functions. */
static void utf8_to_ansi(const char *text, char *out, int out_size)
{
    WCHAR wide[PATH_CHARS];

    utf8_to_wide(text, wide, PATH_CHARS);
    if (!WideCharToMultiByte(CP_ACP, 0, wide, -1, out, out_size, NULL, NULL) && out_size)
        out[0] = '\0';
}

static void ansi_to_utf8(const char *text, char *out, size_t out_size)
{
    WCHAR wide[PATH_CHARS];

    if (!MultiByteToWideChar(CP_ACP, 0, text, -1, wide, PATH_CHARS))
        wide[0] = 0;
    wide_to_utf8(wide, out, out_size);
}

static void push_event(const PlatEvent *ev)
{
    int next = (queue_tail + 1) % QUEUE_SIZE;

    if (next == queue_head)
        return;     /* full: drop */
    queue[queue_tail] = *ev;
    queue_tail = next;
}

static void push_file(const char *path, int flags, PlatWindow *win)
{
    size_t len = strlen(path);
    char *copy;

    if (file_count == file_capacity) {
        int capacity = file_capacity ? file_capacity * 2 : 16;
        FileEvent *grown = realloc(files, sizeof *files * capacity);

        if (!grown)
            return;
        files = grown;
        file_capacity = capacity;
    }
    copy = malloc(len + 1);
    if (!copy)
        return;
    memcpy(copy, path, len + 1);
    files[file_count].path = copy;
    files[file_count].flags = flags;
    files[file_count].win = win;
    file_count++;
}

static int translate_key(WPARAM vk)
{
    switch (vk) {
    case VK_LEFT:   return PK_LEFT;
    case VK_RIGHT:  return PK_RIGHT;
    case VK_UP:     return PK_UP;
    case VK_DOWN:   return PK_DOWN;
    case VK_ESCAPE: return PK_ESCAPE;
    case VK_SPACE:  return ' ';
    case VK_RETURN: return PK_ENTER;
    case VK_DELETE: return PK_DELETE;
    case VK_BACK:   return PK_BACKSPACE;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        return (int)vk;
    return 0;
}

static int current_mods(void)
{
    return (GetKeyState(VK_SHIFT) < 0 ? PMOD_SHIFT : 0) | (GetKeyState(VK_CONTROL) < 0 ? PMOD_CTRL : 0);
}

static void fill_mouse(PlatEvent *ev, PlatWindow *win, LPARAM lparam)
{
    POINT pt;

    pt.x = (short)LOWORD(lparam);
    pt.y = (short)HIWORD(lparam);
    /* Coordinates can be negative while a popup holds the capture. */
    ev->x = pt.x < 0 ? -1 : pt.x * 100 / win->scale;
    ev->y = pt.y < 0 ? -1 : pt.y * 100 / win->scale;
    ClientToScreen(win->hwnd, &pt);
    ev->sx = pt.x;
    ev->sy = pt.y;
    ev->mods = current_mods();
}

/* Media keys, by their virtual-key codes (the names are missing from old
 * headers), and the "application commands" that carry the same requests
 * from headsets, remote controls and the like. */
static const struct { UINT vk; int command; int key; } media_keys[] = {
    { 0xB3, 14, PK_MEDIA_PLAY_PAUSE },      /* VK_MEDIA_PLAY_PAUSE, APPCOMMAND_MEDIA_PLAY_PAUSE */
    { 0xB2, 13, PK_MEDIA_STOP },            /* VK_MEDIA_STOP */
    { 0xB0, 11, PK_MEDIA_NEXT },            /* VK_MEDIA_NEXT_TRACK */
    { 0xB1, 12, PK_MEDIA_PREV },            /* VK_MEDIA_PREV_TRACK */
    { 0, 46, PK_MEDIA_PLAY },               /* APPCOMMAND_MEDIA_PLAY: no key of its own */
    { 0, 47, PK_MEDIA_PAUSE },              /* APPCOMMAND_MEDIA_PAUSE */
};
#define MEDIA_KEYS ((int)(sizeof media_keys / sizeof media_keys[0]))

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    PlatWindow *win = (PlatWindow *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    PlatEvent ev;

    memset(&ev, 0, sizeof ev);
    ev.win = win;
    if (!win)
        return DefWindowProcA(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_CLOSE:
        ev.type = PEV_QUIT;
        push_event(&ev);
        return 0;
    case WM_HOTKEY:
        if (wparam >= 1 && wparam <= (WPARAM)MEDIA_KEYS) {
            ev.type = PEV_KEY_DOWN;
            ev.key = media_keys[wparam - 1].key;
            push_event(&ev);
        }
        return 0;
    case 0x0319: {      /* WM_APPCOMMAND: the command is in the high word, under four flag bits */
        int command = (int)(HIWORD(lparam) & 0x0FFF), i;

        for (i = 0; i < MEDIA_KEYS; i++) {
            if (media_keys[i].command == command) {
                ev.type = PEV_KEY_DOWN;
                ev.key = media_keys[i].key;
                push_event(&ev);
                return 1;       /* handled */
            }
        }
        break;
    }
    case WM_PAINT:
        ValidateRect(hwnd, NULL);
        win->stale = 1;
        ev.type = PEV_EXPOSE;
        push_event(&ev);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        SetCapture(hwnd);
        ev.type = PEV_MOUSE_DOWN;
        ev.button = msg == WM_LBUTTONDOWN ? 1 : 3;
        fill_mouse(&ev, win, lparam);
        push_event(&ev);
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if (!win->popup)    /* a popup keeps the mouse until it is destroyed */
            ReleaseCapture();
        ev.type = PEV_MOUSE_UP;
        ev.button = msg == WM_LBUTTONUP ? 1 : 3;
        fill_mouse(&ev, win, lparam);
        push_event(&ev);
        return 0;
    case WM_MOUSEMOVE:
        ev.type = PEV_MOUSE_MOVE;
        fill_mouse(&ev, win, lparam);
        push_event(&ev);
        return 0;
    case WM_MOUSEWHEEL:
        ev.type = PEV_WHEEL;
        ev.wheel = (short)HIWORD(wparam) > 0 ? 1 : -1;
        push_event(&ev);
        return 0;
    case WM_KEYDOWN:
        ev.key = translate_key(wparam);
        ev.mods = current_mods();
        if (ev.key) {
            ev.type = PEV_KEY_DOWN;
            push_event(&ev);
        }
        return 0;
    case WM_CHAR: {
        /* A typed character in the ANSI code page; reported as UTF-8. */
        char ansi[2] = { (char)wparam, '\0' };

        if (wparam >= 0x20 && wparam != 0x7F && wparam < 0x100) {
            ev.type = PEV_TEXT;
            ansi_to_utf8(ansi, ev.text, sizeof ev.text);
            push_event(&ev);
        }
        return 0;
    }
    case WM_DROPFILES: {
        typedef UINT (WINAPI *QueryFn)(HDROP, UINT, LPWSTR, UINT);
        QueryFn query_wide = unicode_os() ? (QueryFn)(void (*)(void))optional("shell32.dll", "DragQueryFileW") : NULL;
        HDROP drop = (HDROP)wparam;
        UINT count = DragQueryFileA(drop, 0xFFFFFFFF, NULL, 0), i;
        char ansi[MAX_PATH], path[PATH_CHARS * 3];
        WCHAR wide[PATH_CHARS];

        for (i = 0; i < count; i++) {
            if (query_wide && query_wide(drop, i, wide, PATH_CHARS))
                wide_to_utf8(wide, path, sizeof path);
            else if (DragQueryFileA(drop, i, ansi, sizeof ansi))
                ansi_to_utf8(ansi, path, sizeof path);
            else
                continue;
            push_file(path, i == 0 ? PFILE_FIRST : 0, win);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_COPYDATA: {
        /* From plat_instance_send(): NUL-separated paths, ended by an empty one. */
        const COPYDATASTRUCT *data = (const COPYDATASTRUCT *)lparam;
        const char *p = data->lpData, *end = p + data->cbData;
        int first = 1;

        while (p < end && *p && memchr(p, '\0', (size_t)(end - p))) {
            push_file(p, PFILE_REMOTE | (first ? PFILE_FIRST : 0) | (data->dwData ? PFILE_ENQUEUE : 0), win);
            first = 0;
            p += strlen(p) + 1;
        }
        return 1;
    }
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

int plat_init(void)
{
    typedef BOOL (WINAPI *DpiAwareFn)(void);
    DpiAwareFn set_dpi_aware = (DpiAwareFn)(void (*)(void))
        GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDPIAware");
    WNDCLASSA wc;

    /* Without this, Windows Vista and later report 96 dpi and stretch our
     * windows as blurry bitmaps. Looked up at run time because the function
     * does not exist on older systems. */
    if (set_dpi_aware)
        set_dpi_aware();

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    /* Icon 1 comes from packaging/amplitude.rc. */
    wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1));
    if (!wc.hIcon)
        wc.hIcon = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
    wc.lpszClassName = CLASS_NAME;
    return RegisterClassA(&wc) != 0;
}

void plat_shutdown(void)
{
    for (; file_pos < file_count; file_pos++)
        free(files[file_pos].path);
    free(files);
    free(last_path);
    files = NULL;
    last_path = NULL;
    file_count = file_pos = file_capacity = 0;
    if (instance_mutex)
        CloseHandle(instance_mutex);
    instance_mutex = NULL;
}

/* --- Windows ---------------------------------------------------------------- */

static PlatWindow *create_window(const char *title, int w, int h, int scale, int x, int y,
                                 PlatWindow *owner, int popup)
{
    PlatWindow *win = calloc(1, sizeof *win);
    DWORD ex_style = popup ? WS_EX_TOOLWINDOW | WS_EX_TOPMOST : owner ? 0 : WS_EX_APPWINDOW;

    if (!win)
        return NULL;
    win->w = w;
    win->h = h;
    win->scale = scale;
    win->owned = owner != NULL;
    win->popup = popup;
    win->rw = PLAT_SCALED(w, scale);
    win->rh = PLAT_SCALED(h, scale);
    /* WS_POPUP has no border or caption, so client size == window size.
     * Owned popups follow their owner and stay out of the taskbar. */
    win->hwnd = CreateWindowExA(ex_style, CLASS_NAME, title,
                                popup ? WS_POPUP : WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                                x, y, win->rw, win->rh,
                                owner ? owner->hwnd : NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!win->hwnd) {
        free(win);
        return NULL;
    }
    SetWindowLongPtrA(win->hwnd, GWLP_USERDATA, (LONG_PTR)win);
    if (popup) {
        ShowWindow(win->hwnd, SW_SHOWNA);
        SetCapture(win->hwnd);
    } else {
        DragAcceptFiles(win->hwnd, TRUE);
    }
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
    int i;

    if (win->popup)
        ReleaseCapture();
    DestroyWindow(win->hwnd);
    /* Events still queued for this window must not outlive it. */
    for (i = queue_head; i != queue_tail; i = (i + 1) % QUEUE_SIZE)
        if (queue[i].win == win)
            queue[i].type = PEV_NONE;
    free(win->shown);
    free(win);
}

void plat_window_show(PlatWindow *win, int visible)
{
    /* Owned windows appear without taking keyboard focus from the main one. */
    ShowWindow(win->hwnd, !visible ? SW_HIDE : win->owned ? SW_SHOWNA : SW_SHOW);
}

void plat_window_resize(PlatWindow *win, int w, int h)
{
    win->w = w;
    win->h = h;
    win->rw = PLAT_SCALED(w, win->scale);
    win->rh = PLAT_SCALED(h, win->scale);
    SetWindowPos(win->hwnd, NULL, 0, 0, win->rw, win->rh,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void plat_window_present(PlatWindow *win, const uint32_t *pixels)
{
    size_t size = sizeof(uint32_t) * (size_t)win->rw * (size_t)win->rh;
    int top = 0, bottom = win->rh;
    BITMAPINFO bmi;
    HDC dc;

    /* The copy to compare against. Without one (first frame, new size, or
     * no memory) the whole frame goes out. */
    if (win->shown_w != win->rw || win->shown_h != win->rh) {
        free(win->shown);
        win->shown = malloc(size);
        win->shown_w = win->rw;
        win->shown_h = win->rh;
        win->stale = 1;
    }
    if (win->shown && !win->stale)
        plat_changed_rows(win->shown, pixels, win->rw, win->rh, &top, &bottom);
    if (top == bottom)
        return;
    if (win->shown)
        memcpy(win->shown, pixels, size);
    win->stale = !win->shown;

    /* The changed rows, described as a bitmap of their own. */
    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
    bmi.bmiHeader.biWidth = win->rw;
    bmi.bmiHeader.biHeight = -(bottom - top);   /* negative = top-down rows */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    dc = GetDC(win->hwnd);
    SetDIBitsToDevice(dc, 0, top, (DWORD)win->rw, (DWORD)(bottom - top), 0, 0, 0, (UINT)(bottom - top),
                      pixels + (size_t)top * win->rw, &bmi, DIB_RGB_COLORS);
    ReleaseDC(win->hwnd, dc);
}

void plat_window_get_pos(PlatWindow *win, int *sx, int *sy)
{
    RECT rc;

    GetWindowRect(win->hwnd, &rc);
    *sx = rc.left;
    *sy = rc.top;
}

void plat_window_set_pos(PlatWindow *win, int sx, int sy)
{
    SetWindowPos(win->hwnd, NULL, sx, sy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void plat_window_minimize(PlatWindow *win)
{
    ShowWindow(win->hwnd, SW_MINIMIZE);
}

void plat_window_focus(PlatWindow *win)
{
    SetForegroundWindow(win->hwnd);
    SetFocus(win->hwnd);
}

/* --- Events ----------------------------------------------------------------- */

int plat_poll_event(PlatEvent *ev)
{
    MSG msg;

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
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
    while (queue_head != queue_tail) {
        *ev = queue[queue_head];
        queue_head = (queue_head + 1) % QUEUE_SIZE;
        if (ev->type != PEV_NONE)
            return 1;
    }
    return 0;
}

void plat_wait(int timeout_ms)
{
    if (queue_head != queue_tail || file_pos < file_count)
        return;
    MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)timeout_ms, QS_ALLINPUT);
}

uint32_t plat_ticks_ms(void)
{
    return GetTickCount();
}

/* --- Single instance -------------------------------------------------------- */

int plat_instance_send(const char *const *paths, int count, int enqueue)
{
    HWND running = FindWindowA(CLASS_NAME, MAIN_TITLE);
    COPYDATASTRUCT data;
    size_t size = 1, pos = 0;
    char *buffer;
    int i, waited;

    /* Explorer opens a selection of files by starting the program once per
     * file, all in the same moment, before any of them has a window to be
     * found by. The named mutex settles which one becomes the player: the
     * first to create it. The others wait for that one's window to appear
     * and hand their file over. */
    if (!instance_mutex) {
        instance_mutex = CreateMutexA(NULL, FALSE, INSTANCE_MUTEX);
        if (instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            for (waited = 0; !running && waited < INSTANCE_WAIT_MS; waited += INSTANCE_POLL_MS) {
                Sleep(INSTANCE_POLL_MS);
                running = FindWindowA(CLASS_NAME, MAIN_TITLE);
            }
        }
    }
    if (!running)
        return 0;       /* we are the player; the mutex is held until we exit */
    for (i = 0; i < count; i++)
        size += strlen(paths[i]) + 1;
    buffer = malloc(size);
    if (buffer) {
        for (i = 0; i < count; i++) {
            strcpy(buffer + pos, paths[i]);
            pos += strlen(paths[i]) + 1;
        }
        buffer[pos] = '\0';
        data.dwData = (ULONG_PTR)enqueue;
        data.cbData = (DWORD)size;
        data.lpData = buffer;
        SendMessageA(running, WM_COPYDATA, 0, (LPARAM)&data);
        free(buffer);
    }
    if (IsIconic(running))
        ShowWindow(running, SW_RESTORE);
    SetForegroundWindow(running);
    return 1;
}

void plat_instance_claim(PlatWindow *main_window)
{
    int i;

    /* Later instances find the main window by its title, so there is
     * nothing to claim for that. Being the player is when the media keys
     * become ours: they are registered as hot keys so that they work
     * whichever program has the keyboard. Registering fails harmlessly if
     * another program has them, and ends with the window. */
    for (i = 0; i < MEDIA_KEYS; i++)
        if (media_keys[i].vk)
            RegisterHotKey(main_window->hwnd, i + 1, 0, media_keys[i].vk);
}

void plat_media_update(int state, const char *title, double position, double length)
{
    /* Windows has nowhere to show this short of the modern media overlay,
     * which is out of reach of a program that also runs on old systems. */
    (void)state;
    (void)title;
    (void)position;
    (void)length;
}

/* --- Dialogs ---------------------------------------------------------------- */

#define FILTER(T) T("Audio files\0*.mp3;*.flac;*.wav;*.ogg;*.oga;*.opus;*.m4a;*.mp4;*.aac;") \
                  T("*.mod;*.xm;*.s3m;*.it;*.m3u\0Skins (*.wsz)\0*.wsz;*.zip\0All files\0*.*\0")
#define NARROW(x) x
#define WIDE(x)   L##x

int plat_open_files_dialog(PlatPathFn fn, void *user)
{
    typedef BOOL (WINAPI *OpenFn)(LPOPENFILENAMEW);
    static WCHAR wide[32768];
    static char ansi[32768];
    OpenFn open_wide = unicode_os() ? (OpenFn)(void (*)(void))optional("comdlg32.dll", "GetOpenFileNameW") : NULL;
    char dir[PATH_CHARS * 3], name[PATH_CHARS * 3], path[PATH_CHARS * 6];
    int count = 0;

    /* Windows 9x/NT4 reject the larger structs introduced with Windows 2000,
     * so the original size is given in both cases. */
    if (open_wide) {
        OPENFILENAMEW ofn;
        const WCHAR *item;

        memset(&ofn, 0, sizeof ofn);
        wide[0] = 0;
        ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400W;
        ofn.lpstrFilter = FILTER(WIDE);
        ofn.lpstrFile = wide;
        ofn.nMaxFile = sizeof wide / sizeof wide[0];
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
        if (!open_wide(&ofn))
            return 0;
        /* One file: a single full path. Several: the directory, then each name. */
        wide_to_utf8(wide, dir, sizeof dir);
        item = wide + lstrlenW(wide) + 1;
        if (!*item) {
            fn(dir, user);
            return 1;
        }
        for (; *item; item += lstrlenW(item) + 1, count++) {
            wide_to_utf8(item, name, sizeof name);
            snprintf(path, sizeof path, "%s%s%s", dir, dir[strlen(dir) - 1] == '\\' ? "" : "\\", name);
            fn(path, user);
        }
    } else {
        OPENFILENAMEA ofn;
        const char *item;

        memset(&ofn, 0, sizeof ofn);
        ansi[0] = '\0';
        ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400A;
        ofn.lpstrFilter = FILTER(NARROW);
        ofn.lpstrFile = ansi;
        ofn.nMaxFile = sizeof ansi;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
        if (!GetOpenFileNameA(&ofn))
            return 0;
        ansi_to_utf8(ansi, dir, sizeof dir);
        item = ansi + strlen(ansi) + 1;
        if (!*item) {
            fn(dir, user);
            return 1;
        }
        for (; *item; item += strlen(item) + 1, count++) {
            ansi_to_utf8(item, name, sizeof name);
            snprintf(path, sizeof path, "%s%s%s", dir, dir[strlen(dir) - 1] == '\\' ? "" : "\\", name);
            fn(path, user);
        }
    }
    return count;
}

void plat_open_url(const char *url)
{
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

int plat_save_file_dialog(char *out, size_t out_size)
{
    typedef BOOL (WINAPI *SaveFn)(LPOPENFILENAMEW);
    SaveFn save_wide = unicode_os() ? (SaveFn)(void (*)(void))optional("comdlg32.dll", "GetSaveFileNameW") : NULL;

    /* The original struct size again; see plat_open_files_dialog(). */
    if (save_wide) {
        WCHAR wide[PATH_CHARS] = L"";
        OPENFILENAMEW ofn;

        memset(&ofn, 0, sizeof ofn);
        ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400W;
        ofn.lpstrFilter = L"Playlists (*.m3u)\0*.m3u\0All files\0*.*\0";
        ofn.lpstrFile = wide;
        ofn.nMaxFile = PATH_CHARS;
        ofn.lpstrDefExt = L"m3u";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
        if (!save_wide(&ofn))
            return 0;
        wide_to_utf8(wide, out, out_size);
    } else {
        char ansi[PATH_CHARS] = "";
        OPENFILENAMEA ofn;

        memset(&ofn, 0, sizeof ofn);
        ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400A;
        ofn.lpstrFilter = "Playlists (*.m3u)\0*.m3u\0All files\0*.*\0";
        ofn.lpstrFile = ansi;
        ofn.nMaxFile = PATH_CHARS;
        ofn.lpstrDefExt = "m3u";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameA(&ofn))
            return 0;
        ansi_to_utf8(ansi, out, out_size);
    }
    return out[0] != '\0';
}

int plat_open_folder_dialog(char *out, size_t out_size)
{
    typedef LPITEMIDLIST (WINAPI *BrowseFn)(LPBROWSEINFOW);
    typedef BOOL (WINAPI *PathFn)(LPCITEMIDLIST, LPWSTR);
    BrowseFn browse_wide = NULL;
    PathFn path_wide = NULL;
    char path[PATH_CHARS * 3] = "";
    LPITEMIDLIST item;
    LPMALLOC allocator;

    if (unicode_os()) {
        browse_wide = (BrowseFn)(void (*)(void))optional("shell32.dll", "SHBrowseForFolderW");
        path_wide = (PathFn)(void (*)(void))optional("shell32.dll", "SHGetPathFromIDListW");
    }
    if (browse_wide && path_wide) {
        WCHAR wide[PATH_CHARS];
        BROWSEINFOW info;

        memset(&info, 0, sizeof info);
        info.lpszTitle = L"Add folder";
        info.ulFlags = BIF_RETURNONLYFSDIRS;
        item = browse_wide(&info);
        if (item && path_wide(item, wide))
            wide_to_utf8(wide, path, sizeof path);
    } else {
        char ansi[MAX_PATH];
        BROWSEINFOA info;

        memset(&info, 0, sizeof info);
        info.lpszTitle = "Add folder";
        info.ulFlags = BIF_RETURNONLYFSDIRS;
        item = SHBrowseForFolderA(&info);
        if (item && SHGetPathFromIDListA(item, ansi))
            ansi_to_utf8(ansi, path, sizeof path);
    }
    if (item && SHGetMalloc(&allocator) == NOERROR) {
        allocator->lpVtbl->Free(allocator, item);
        allocator->lpVtbl->Release(allocator);
    }
    if (!path[0] || strlen(path) >= out_size)
        return 0;
    strcpy(out, path);
    return 1;
}

/* --- Files ------------------------------------------------------------------ */

FILE *plat_fopen(const char *path, const char *mode)
{
    char ansi[MAX_PATH * 2];

    if (unicode_os()) {
        WCHAR wide[PATH_CHARS], wide_mode[8];

        utf8_to_wide(path, wide, PATH_CHARS);
        utf8_to_wide(mode, wide_mode, 8);
        return _wfopen(wide, wide_mode);
    }
    utf8_to_ansi(path, ansi, sizeof ansi);
    return fopen(ansi, mode);
}

static DWORD attributes(const char *path)
{
    char ansi[MAX_PATH * 2];

    if (unicode_os()) {
        WCHAR wide[PATH_CHARS];

        utf8_to_wide(path, wide, PATH_CHARS);
        return GetFileAttributesW(wide);
    }
    utf8_to_ansi(path, ansi, sizeof ansi);
    return GetFileAttributesA(ansi);
}

int plat_is_dir(const char *path)
{
    DWORD attrs = attributes(path);

    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

int plat_list_dir(const char *path, PlatPathFn fn, void *user)
{
    char pattern[PATH_CHARS * 3], name[PATH_CHARS * 3], full[PATH_CHARS * 6];
    HANDLE find;
    int more;

    if (strlen(path) + 3 > sizeof pattern)
        return 0;
    strcpy(pattern, path);
    strcat(pattern, "\\*");

    if (unicode_os()) {
        WCHAR wide[PATH_CHARS];
        WIN32_FIND_DATAW entry;

        utf8_to_wide(pattern, wide, PATH_CHARS);
        find = FindFirstFileW(wide, &entry);
        for (more = find != INVALID_HANDLE_VALUE; more; more = FindNextFileW(find, &entry)) {
            wide_to_utf8(entry.cFileName, name, sizeof name);
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            snprintf(full, sizeof full, "%s\\%s", path, name);
            fn(full, user);
        }
    } else {
        char ansi[MAX_PATH * 2];
        WIN32_FIND_DATAA entry;

        utf8_to_ansi(pattern, ansi, sizeof ansi);
        find = FindFirstFileA(ansi, &entry);
        for (more = find != INVALID_HANDLE_VALUE; more; more = FindNextFileA(find, &entry)) {
            ansi_to_utf8(entry.cFileName, name, sizeof name);
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            snprintf(full, sizeof full, "%s\\%s", path, name);
            fn(full, user);
        }
    }
    if (find == INVALID_HANDLE_VALUE)
        return 0;
    FindClose(find);
    return 1;
}

int plat_absolute_path(const char *path, char *out, size_t out_size)
{
    char result[PATH_CHARS * 3] = "";

    if (unicode_os()) {
        WCHAR wide[PATH_CHARS], full[PATH_CHARS];
        DWORD len;

        utf8_to_wide(path, wide, PATH_CHARS);
        len = GetFullPathNameW(wide, PATH_CHARS, full, NULL);
        if (len > 0 && len < PATH_CHARS)
            wide_to_utf8(full, result, sizeof result);
    } else {
        char ansi[MAX_PATH * 2], full[MAX_PATH * 2];
        DWORD len;

        utf8_to_ansi(path, ansi, sizeof ansi);
        len = GetFullPathNameA(ansi, sizeof full, full, NULL);
        if (len > 0 && len < sizeof full)
            ansi_to_utf8(full, result, sizeof result);
    }
    if (!result[0] || strlen(result) >= out_size)
        return 0;
    strcpy(out, result);
    return 1;
}

/* Portable mode: if amplitude.ini sits next to the executable, settings stay
 * there. Otherwise they go to %APPDATA%\Amplitude, or next to the executable
 * on systems (such as Windows 9x) that have no such folder. */
int plat_config_dir(char *out, size_t out_size)
{
    char exe_dir[PATH_CHARS * 3] = "", appdata[PATH_CHARS * 3] = "", probe[PATH_CHARS * 3 + 16];
    char *slash;

    if (unicode_os()) {
        WCHAR wide[PATH_CHARS];

        if (GetModuleFileNameW(NULL, wide, PATH_CHARS))
            wide_to_utf8(wide, exe_dir, sizeof exe_dir);
        if (GetEnvironmentVariableW(L"APPDATA", wide, PATH_CHARS))
            wide_to_utf8(wide, appdata, sizeof appdata);
    } else {
        char ansi[MAX_PATH * 2];

        if (GetModuleFileNameA(NULL, ansi, sizeof ansi))
            ansi_to_utf8(ansi, exe_dir, sizeof exe_dir);
        if (GetEnvironmentVariableA("APPDATA", ansi, sizeof ansi))
            ansi_to_utf8(ansi, appdata, sizeof appdata);
    }
    slash = strrchr(exe_dir, '\\');
    if (!slash)
        return 0;
    slash[1] = '\0';
    snprintf(probe, sizeof probe, "%samplitude.ini", exe_dir);

    if (attributes(probe) == INVALID_FILE_ATTRIBUTES && appdata[0] &&
        strlen(appdata) + sizeof "\\Amplitude\\" <= out_size) {
        snprintf(out, out_size, "%s\\Amplitude", appdata);
        if (unicode_os()) {
            WCHAR wide[PATH_CHARS];

            utf8_to_wide(out, wide, PATH_CHARS);
            CreateDirectoryW(wide, NULL);
        } else {
            char ansi[MAX_PATH * 2];

            utf8_to_ansi(out, ansi, sizeof ansi);
            CreateDirectoryA(ansi, NULL);
        }
        strcat(out, "\\");
        return 1;
    }
    if (strlen(exe_dir) >= out_size)
        return 0;
    strcpy(out, exe_dir);
    return 1;
}

void plat_args(int *argc, char ***argv)
{
    typedef LPWSTR *(WINAPI *SplitFn)(LPCWSTR, int *);
    SplitFn split = unicode_os() ? (SplitFn)(void (*)(void))optional("shell32.dll", "CommandLineToArgvW") : NULL;
    int count = *argc, i;
    LPWSTR *wide = split ? split(GetCommandLineW(), &count) : NULL;
    char **out;

    if (!wide)
        count = *argc;      /* a failed split may have changed it */
    out = calloc((size_t)count + 1, sizeof *out);
    for (i = 0; out && i < count; i++) {
        /* Up to four bytes of UTF-8 for each UTF-16 unit or ANSI byte. */
        size_t size = 4 * (wide ? (size_t)lstrlenW(wide[i]) : strlen((*argv)[i])) + 1;

        out[i] = malloc(size);
        if (!out[i]) {      /* out of memory: keep the arguments as they came */
            while (i)
                free(out[--i]);
            free(out);
            out = NULL;
        } else if (wide) {
            wide_to_utf8(wide[i], out[i], size);
        } else {
            ansi_to_utf8((*argv)[i], out[i], size);
        }
    }
    if (wide)
        LocalFree(wide);
    if (!out)
        return;
    /* The new list lives as long as the program does. */
    *argc = count;
    *argv = out;
}

/* --- Screen ----------------------------------------------------------------- */

/* The system dpi: 96 = 100%. */
int plat_default_scale(void)
{
    HDC dc = GetDC(NULL);
    int percent = (GetDeviceCaps(dc, LOGPIXELSX) * 100 + 48) / 96;

    ReleaseDC(NULL, dc);
    return percent < 100 ? 100 : percent > 400 ? 400 : percent;
}

void plat_screen_rect(int sx, int sy, int *x, int *y, int *w, int *h)
{
    /* The monitor functions arrived with Windows 98; without them there is
     * only the one primary screen. */
    typedef HMONITOR (WINAPI *FromPointFn)(POINT, DWORD);
    typedef BOOL (WINAPI *InfoFn)(HMONITOR, LPMONITORINFO);
    FromPointFn from_point = (FromPointFn)(void (*)(void))optional("user32.dll", "MonitorFromPoint");
    InfoFn get_info = (InfoFn)(void (*)(void))optional("user32.dll", "GetMonitorInfoA");
    MONITORINFO info;
    POINT point;

    point.x = sx;
    point.y = sy;
    info.cbSize = sizeof info;
    if (from_point && get_info && get_info(from_point(point, MONITOR_DEFAULTTONEAREST), &info)) {
        *x = info.rcWork.left;      /* the work area leaves out the taskbar */
        *y = info.rcWork.top;
        *w = info.rcWork.right - info.rcWork.left;
        *h = info.rcWork.bottom - info.rcWork.top;
        return;
    }
    *x = *y = 0;
    *w = GetSystemMetrics(SM_CXSCREEN);
    *h = GetSystemMetrics(SM_CYSCREEN);
}
