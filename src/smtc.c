/* The Windows media overlay (see smtc.h).
 *
 * The overlay is reached through the Windows Runtime, which old systems do
 * not have: its few functions are looked up when the player starts, so the
 * same executable still loads where they are missing. Its headers are not
 * needed either (old compilers lack them). The interfaces used are declared
 * here, each as the table of functions it is, with the entries that go
 * unused as placeholders; their layout is fixed for good once published.
 *
 * Only what the overlay needs is implemented: the transport buttons, the
 * state and the title. The position, seeking and cover art are left out. */
#include "smtc.h"

#include <string.h>

/* From the Windows Runtime headers. */
typedef void *HString;
typedef struct { INT64 value; } Token;      /* EventRegistrationToken */
enum { APARTMENT_THREADED = 2 };            /* COINIT_APARTMENTTHREADED */
enum { STATUS_STOPPED = 2, STATUS_PLAYING = 3, STATUS_PAUSED = 4 };     /* MediaPlaybackStatus */
enum { TYPE_MUSIC = 1 };                    /* MediaPlaybackType */
enum { BUTTON_PLAY, BUTTON_PAUSE, BUTTON_STOP, BUTTON_NEXT = 6, BUTTON_PREVIOUS };  /* SystemMediaTransportControlsButton */

static const GUID IID_UNKNOWN  = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_AGILE    = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_INTEROP  = { 0xDDB0472D, 0xC911, 0x4A1F, { 0x86, 0xD9, 0xDC, 0x3D, 0x71, 0xA9, 0x5F, 0x5A } };
static const GUID IID_CONTROLS = { 0x99FA3FF4, 0x1742, 0x42A6, { 0x90, 0x2E, 0x08, 0x7D, 0x41, 0xF9, 0x65, 0xEC } };
/* TypedEventHandler<SystemMediaTransportControls, SystemMediaTransportControlsButtonPressedEventArgs> */
static const GUID IID_HANDLER  = { 0x0557E996, 0x7B23, 0x5BAE, { 0xAA, 0x81, 0xEA, 0x0D, 0x67, 0x11, 0x43, 0xA4 } };

typedef struct Interop  { const struct InteropVtbl *v; } Interop;       /* ISystemMediaTransportControlsInterop */
typedef struct Controls { const struct ControlsVtbl *v; } Controls;     /* ISystemMediaTransportControls */
typedef struct Updater  { const struct UpdaterVtbl *v; } Updater;       /* ISystemMediaTransportControlsDisplayUpdater */
typedef struct Music    { const struct MusicVtbl *v; } Music;           /* IMusicDisplayProperties */
typedef struct Pressed  { const struct PressedVtbl *v; } Pressed;       /* ISystemMediaTransportControlsButtonPressedEventArgs */
typedef struct Handler  { const struct HandlerVtbl *v; } Handler;       /* the event handler, which is ours */

/* Every interface starts with these three; those of the Windows Runtime
 * go on with three more that tell what an object is. */
#define UNKNOWN(T) \
    HRESULT (WINAPI *QueryInterface)(T *, const GUID *iid, void **out); \
    ULONG (WINAPI *AddRef)(T *); \
    ULONG (WINAPI *Release)(T *)
#define INSPECTABLE(T) UNKNOWN(T); void *inspectable[3]

struct InteropVtbl {
    INSPECTABLE(Interop);
    HRESULT (WINAPI *GetForWindow)(Interop *, HWND window, const GUID *iid, void **controls);
};

struct ControlsVtbl {
    INSPECTABLE(Controls);
    void *get_PlaybackStatus;
    HRESULT (WINAPI *put_PlaybackStatus)(Controls *, int status);
    HRESULT (WINAPI *get_DisplayUpdater)(Controls *, Updater **updater);
    void *get_SoundLevel;
    void *get_IsEnabled;
    HRESULT (WINAPI *put_IsEnabled)(Controls *, BYTE enabled);
    void *get_IsPlayEnabled;
    HRESULT (WINAPI *put_IsPlayEnabled)(Controls *, BYTE enabled);
    void *get_IsStopEnabled;
    HRESULT (WINAPI *put_IsStopEnabled)(Controls *, BYTE enabled);
    void *get_IsPauseEnabled;
    HRESULT (WINAPI *put_IsPauseEnabled)(Controls *, BYTE enabled);
    void *record[2], *fast_forward[2], *rewind[2];
    void *get_IsPreviousEnabled;
    HRESULT (WINAPI *put_IsPreviousEnabled)(Controls *, BYTE enabled);
    void *get_IsNextEnabled;
    HRESULT (WINAPI *put_IsNextEnabled)(Controls *, BYTE enabled);
    void *channel_up[2], *channel_down[2];
    HRESULT (WINAPI *add_ButtonPressed)(Controls *, Handler *handler, Token *token);
    HRESULT (WINAPI *remove_ButtonPressed)(Controls *, Token token);
};

struct UpdaterVtbl {
    INSPECTABLE(Updater);
    void *get_Type;
    HRESULT (WINAPI *put_Type)(Updater *, int type);
    void *app_media_id[2], *thumbnail[2];
    HRESULT (WINAPI *get_MusicProperties)(Updater *, Music **music);
    void *get_VideoProperties, *get_ImageProperties, *CopyFromFileAsync;
    HRESULT (WINAPI *ClearAll)(Updater *);
    HRESULT (WINAPI *Update)(Updater *);
};

struct MusicVtbl {
    INSPECTABLE(Music);
    void *get_Title;
    HRESULT (WINAPI *put_Title)(Music *, HString title);
};

struct PressedVtbl {
    INSPECTABLE(Pressed);
    HRESULT (WINAPI *get_Button)(Pressed *, int *button);
};

struct HandlerVtbl {
    UNKNOWN(Handler);
    HRESULT (WINAPI *Invoke)(Handler *, Controls *sender, Pressed *pressed);
};

static struct {
    HRESULT (WINAPI *initialize)(void *reserved, DWORD model);
    void (WINAPI *uninitialize)(void);
    HRESULT (WINAPI *get_factory)(HString class_name, const GUID *iid, void **factory);
    HRESULT (WINAPI *create_string)(const WCHAR *text, UINT32 length, HString *out);
    HRESULT (WINAPI *delete_string)(HString text);
} rt;

static const struct { const char *dll; const char *name; void *slot; } symbols[] = {
    { "combase.dll", "RoGetActivationFactory", &rt.get_factory },      /* the one old systems lack */
    { "combase.dll", "WindowsCreateString", &rt.create_string },
    { "combase.dll", "WindowsDeleteString", &rt.delete_string },
    { "ole32.dll", "CoInitializeEx", &rt.initialize },
    { "ole32.dll", "CoUninitialize", &rt.uninitialize },
};

static int loaded;          /* 1 once the functions are found, -1 if they are not there */
static int com_started;     /* we were the ones to start COM on this thread */
static Controls *controls;
static Token token;
static HWND window;
static UINT message;

/* What the overlay was told. */
static int sent, state;
static char title[512];

/* --- The button handler ------------------------------------------------------ */

/* There is one handler and it lives as long as the program, so it is not
 * counted. Calling itself "agile" lets the system call it from whichever
 * thread it likes, which is why all it does is post a message. */
static HRESULT WINAPI handler_query(Handler *self, const GUID *iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (memcmp(iid, &IID_UNKNOWN, sizeof *iid) != 0 && memcmp(iid, &IID_AGILE, sizeof *iid) != 0 &&
        memcmp(iid, &IID_HANDLER, sizeof *iid) != 0) {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = self;
    return S_OK;
}

static ULONG WINAPI handler_addref(Handler *self)
{
    (void)self;
    return 2;
}

static ULONG WINAPI handler_release(Handler *self)
{
    (void)self;
    return 1;
}

static HRESULT WINAPI handler_invoke(Handler *self, Controls *sender, Pressed *pressed)
{
    HWND target = window;
    int button = -1, command;

    (void)self;
    (void)sender;
    if (!target || !pressed || FAILED(pressed->v->get_Button(pressed, &button)))
        return S_OK;
    switch (button) {
    case BUTTON_PLAY:     command = SMTC_PLAY; break;
    case BUTTON_PAUSE:    command = SMTC_PAUSE; break;
    case BUTTON_STOP:     command = SMTC_STOP; break;
    case BUTTON_NEXT:     command = SMTC_NEXT; break;
    case BUTTON_PREVIOUS: command = SMTC_PREVIOUS; break;
    default:              return S_OK;
    }
    PostMessageA(target, message, (WPARAM)command, 0);
    return S_OK;
}

static const struct HandlerVtbl handler_vtbl = { handler_query, handler_addref, handler_release, handler_invoke };
static Handler handler = { &handler_vtbl };

/* --- The interface ----------------------------------------------------------- */

static int load(void)
{
    size_t i;

    if (loaded)
        return loaded > 0;
    loaded = -1;
    for (i = 0; i < sizeof symbols / sizeof symbols[0]; i++) {
        HMODULE module = LoadLibraryA(symbols[i].dll);
        FARPROC address = module ? GetProcAddress(module, symbols[i].name) : NULL;

        if (!address)
            return 0;
        memcpy(symbols[i].slot, &address, sizeof address);     /* a function pointer, stored as such */
    }
    loaded = 1;
    return 1;
}

int smtc_attach(HWND new_window, UINT new_message)
{
    static const WCHAR class_name[] = L"Windows.Media.SystemMediaTransportControls";
    Interop *interop = NULL;
    HString name = NULL;

    smtc_detach(window);
    if (!load())
        return 0;
    /* Fails if the thread already uses COM in another way, which will do. */
    com_started = SUCCEEDED(rt.initialize(NULL, APARTMENT_THREADED));
    if (SUCCEEDED(rt.create_string(class_name, sizeof class_name / sizeof class_name[0] - 1, &name))) {
        if (FAILED(rt.get_factory(name, &IID_INTEROP, (void **)&interop)))
            interop = NULL;
        rt.delete_string(name);
    }
    if (interop) {
        if (FAILED(interop->v->GetForWindow(interop, new_window, &IID_CONTROLS, (void **)&controls)))
            controls = NULL;
        interop->v->Release(interop);
    }
    window = new_window;
    message = new_message;
    sent = 0;
    if (!controls || FAILED(controls->v->add_ButtonPressed(controls, &handler, &token))) {
        token.value = 0;
        smtc_detach(window);
        return 0;
    }
    controls->v->put_IsPlayEnabled(controls, 1);
    controls->v->put_IsPauseEnabled(controls, 1);
    controls->v->put_IsStopEnabled(controls, 1);
    controls->v->put_IsNextEnabled(controls, 1);
    controls->v->put_IsPreviousEnabled(controls, 1);
    controls->v->put_IsEnabled(controls, 1);
    return 1;
}

void smtc_detach(HWND old_window)
{
    if (!window || old_window != window)
        return;
    window = NULL;      /* a button pressed from here on goes nowhere */
    if (controls) {
        if (token.value)
            controls->v->remove_ButtonPressed(controls, token);
        controls->v->put_IsEnabled(controls, 0);
        controls->v->Release(controls);
    }
    controls = NULL;
    token.value = 0;
    if (com_started)
        rt.uninitialize();
    com_started = 0;
}

/* Shows the title, or takes down what was shown if there is none. */
static void show_title(void)
{
    WCHAR wide[sizeof title];
    int length = MultiByteToWideChar(CP_UTF8, 0, title, -1, wide, sizeof wide / sizeof wide[0]) - 1;
    Updater *updater = NULL;
    Music *music = NULL;
    HString text = NULL;

    if (FAILED(controls->v->get_DisplayUpdater(controls, &updater)) || !updater)
        return;
    if (length <= 0) {
        updater->v->ClearAll(updater);
    } else {
        updater->v->put_Type(updater, TYPE_MUSIC);
        if (SUCCEEDED(updater->v->get_MusicProperties(updater, &music)) && music) {
            if (SUCCEEDED(rt.create_string(wide, (UINT32)length, &text))) {
                music->v->put_Title(music, text);
                rt.delete_string(text);
            }
            music->v->Release(music);
        }
        updater->v->Update(updater);
    }
    updater->v->Release(updater);
}

void smtc_update(int new_state, const char *new_title)
{
    /* Only as much of a title as is kept is compared, or an overlong one
     * would count as new every time. */
    int retitled = !sent || strncmp(new_title, title, sizeof title - 1) != 0;

    if (!controls || (!retitled && new_state == state))
        return;
    sent = 1;
    state = new_state;
    if (retitled) {
        strncpy(title, new_title, sizeof title - 1);
        title[sizeof title - 1] = '\0';
        show_title();
    }
    controls->v->put_PlaybackStatus(controls, state == 1 ? STATUS_PLAYING : state == 2 ? STATUS_PAUSED : STATUS_STOPPED);
}
