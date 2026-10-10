/* Network connections and threads for Windows (see platform.h).
 *
 * Nothing here adds to the DLLs the program needs in order to start: both
 * libraries are loaded when the first stream is opened.
 *
 * Plain connections use Winsock 1.1 (wsock32.dll, present since Windows 95)
 * directly, so that stream.c speaks to the server itself; old Shoutcast
 * servers answer in a way WinINet does not accept. Secure addresses are
 * handed to WinINet (wininet.dll, part of Internet Explorer) as a whole,
 * which brings the system's own encryption and certificates. How modern a
 * server it can talk to therefore depends on the Windows version. */
#include "platform.h"
#include "win32_library.h"

#include <stdlib.h>
#include <windows.h>
#include <winsock.h>

#define NET_TIMEOUT_MS 15000
#define CONNECT_TIMEOUT_MS 5000     /* for each of a server's addresses */
#define ADDRESSES_MAX  8

/* From wininet.h, which old toolchains may lack. */
typedef void *NetHandle;
#define NET_OPEN_PRECONFIG   0
#define NET_FLAG_RELOAD      0x80000000u
#define NET_FLAG_NO_CACHE    0x04000000u
#define NET_QUERY_RAW_HEADERS_CRLF 22

struct PlatConn {
    SOCKET fd;              /* INVALID_SOCKET for a WinINet connection */
    NetHandle session, request;
    volatile LONG aborted;
};

static struct {
    int (WINAPI *WSAStartup)(WORD, LPWSADATA);
    SOCKET (WINAPI *socket)(int, int, int);
    int (WINAPI *connect)(SOCKET, const struct sockaddr *, int);
    int (WINAPI *send)(SOCKET, const char *, int, int);
    int (WINAPI *recv)(SOCKET, char *, int, int);
    int (WINAPI *closesocket)(SOCKET);
    int (WINAPI *shutdown)(SOCKET, int);
    int (WINAPI *setsockopt)(SOCKET, int, int, const char *, int);
    int (WINAPI *ioctlsocket)(SOCKET, long, u_long *);
    int (WINAPI *select)(int, fd_set *, fd_set *, fd_set *, const struct timeval *);
    struct hostent *(WINAPI *gethostbyname)(const char *);
    u_short (WINAPI *htons)(u_short);
} sock;

static struct {
    NetHandle (WINAPI *InternetOpenA)(LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD);
    NetHandle (WINAPI *InternetOpenUrlA)(NetHandle, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR);
    BOOL (WINAPI *HttpQueryInfoA)(NetHandle, DWORD, LPVOID, LPDWORD, LPDWORD);
    BOOL (WINAPI *InternetReadFile)(NetHandle, LPVOID, DWORD, LPDWORD);
    BOOL (WINAPI *InternetCloseHandle)(NetHandle);
} inet;

typedef struct { const char *name; void *slot; } Symbol;

/* Loads a library and looks all the symbols up. Returns 0 if any is missing. */
static int load(const char *dll, const Symbol *symbols, int count)
{
    HMODULE module = win32_system_library(dll);
    int i;

    for (i = 0; module && i < count; i++) {
        FARPROC address = GetProcAddress(module, symbols[i].name);

        if (!address)
            return 0;
        memcpy(symbols[i].slot, &address, sizeof address);
    }
    return module != NULL;
}

static int sockets_ready(void)
{
    static const Symbol symbols[] = {
        { "WSAStartup", &sock.WSAStartup }, { "socket", &sock.socket }, { "connect", &sock.connect },
        { "send", &sock.send }, { "recv", &sock.recv }, { "closesocket", &sock.closesocket },
        { "shutdown", &sock.shutdown }, { "setsockopt", &sock.setsockopt },
        { "ioctlsocket", &sock.ioctlsocket }, { "select", &sock.select },
        { "gethostbyname", &sock.gethostbyname }, { "htons", &sock.htons } };
    static LONG state;      /* 0 untried, 1 ready, -1 unavailable */
    WSADATA data;

    if (!state)
        InterlockedExchange(&state, load("wsock32.dll", symbols, (int)(sizeof symbols / sizeof symbols[0])) &&
                                    sock.WSAStartup(MAKEWORD(1, 1), &data) == 0 ? 1 : -1);
    return state > 0;
}

static int wininet_ready(void)
{
    static const Symbol symbols[] = {
        { "InternetOpenA", &inet.InternetOpenA }, { "InternetOpenUrlA", &inet.InternetOpenUrlA },
        { "HttpQueryInfoA", &inet.HttpQueryInfoA }, { "InternetReadFile", &inet.InternetReadFile },
        { "InternetCloseHandle", &inet.InternetCloseHandle } };
    static LONG state;

    if (!state)
        InterlockedExchange(&state, load("wininet.dll", symbols, (int)(sizeof symbols / sizeof symbols[0])) ? 1 : -1);
    return state > 0;
}

/* connect() with a limit of its own: left to itself it waits some twenty
 * seconds on an address that does not answer. The sets are filled in and
 * read by hand; the usual macros call into the socket library, which is
 * not linked. */
static int connect_within(SOCKET fd, const struct sockaddr_in *address, int ms)
{
    struct timeval limit = { ms / 1000, ms % 1000 * 1000 };
    fd_set writable, failed;
    u_long on = 1, off = 0;
    int ok;

    sock.ioctlsocket(fd, FIONBIO, &on);
    ok = sock.connect(fd, (const struct sockaddr *)address, sizeof *address) == 0;
    if (!ok) {      /* under way: done when the socket can be written to */
        writable.fd_count = failed.fd_count = 1;
        writable.fd_array[0] = failed.fd_array[0] = fd;
        ok = sock.select(0, NULL, &writable, &failed, &limit) > 0 && writable.fd_count == 1;
    }
    sock.ioctlsocket(fd, FIONBIO, &off);
    return ok;
}

PlatConn *plat_net_connect(const char *host, int port, int secure)
{
    struct in_addr addresses[ADDRESSES_MAX];
    struct hostent *entry;
    int timeout = NET_TIMEOUT_MS, count = 0, i;
    PlatConn *conn;

    if (secure || !sockets_ready())
        return NULL;        /* secure addresses go through plat_https_get() */
    entry = sock.gethostbyname(host);
    if (!entry || entry->h_addrtype != AF_INET)
        return NULL;
    /* Copied, as the list is only good until the next call into the library. */
    while (count < ADDRESSES_MAX && entry->h_addr_list[count]) {
        memcpy(&addresses[count], entry->h_addr_list[count], sizeof addresses[count]);
        count++;
    }
    conn = calloc(1, sizeof *conn);
    if (!conn)
        return NULL;
    /* A name often stands for several servers, and one of them may be
     * down: each address is tried in turn. */
    for (i = 0; i < count; i++) {
        struct sockaddr_in address;
        SOCKET fd = sock.socket(AF_INET, SOCK_STREAM, 0);

        if (fd == INVALID_SOCKET)
            break;
        sock.setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout);
        sock.setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof timeout);
        memset(&address, 0, sizeof address);
        address.sin_family = AF_INET;
        address.sin_port = sock.htons((u_short)port);
        address.sin_addr = addresses[i];
        if (connect_within(fd, &address, CONNECT_TIMEOUT_MS)) {
            conn->fd = fd;
            return conn;
        }
        sock.closesocket(fd);
    }
    free(conn);
    return NULL;
}

PlatConn *plat_https_get(const char *url, const char *request_headers, char *response, size_t response_size)
{
    DWORD size = (DWORD)response_size - 1;
    PlatConn *conn;

    if (!wininet_ready())
        return NULL;
    conn = calloc(1, sizeof *conn);
    if (!conn)
        return NULL;
    conn->fd = INVALID_SOCKET;
    conn->session = inet.InternetOpenA("Amplitude", NET_OPEN_PRECONFIG, NULL, NULL, 0);
    if (conn->session)
        conn->request = inet.InternetOpenUrlA(conn->session, url, request_headers, (DWORD)-1,
                                              NET_FLAG_RELOAD | NET_FLAG_NO_CACHE, 0);
    /* The header lines as the server sent them, which is what stream.c reads. */
    if (!conn->request || !inet.HttpQueryInfoA(conn->request, NET_QUERY_RAW_HEADERS_CRLF, response, &size, NULL)) {
        plat_net_close(conn);
        return NULL;
    }
    response[size] = '\0';
    return conn;
}

int plat_net_send(PlatConn *conn, const void *data, int size)
{
    const char *p = data;

    if (conn->fd == INVALID_SOCKET)
        return 0;
    while (size > 0) {
        int sent = sock.send(conn->fd, p, size, 0);

        if (sent <= 0)
            return 0;
        p += sent;
        size -= sent;
    }
    return 1;
}

int plat_net_recv(PlatConn *conn, void *buffer, int size)
{
    DWORD got = 0;

    if (conn->fd != INVALID_SOCKET)
        return sock.recv(conn->fd, buffer, size, 0);
    if (conn->aborted || !inet.InternetReadFile(conn->request, buffer, (DWORD)size, &got))
        return -1;
    return (int)got;
}

void plat_net_abort(PlatConn *conn)
{
    if (conn->fd != INVALID_SOCKET) {
        sock.shutdown(conn->fd, 2);     /* both directions */
    } else if (!InterlockedExchange(&conn->aborted, 1) && conn->request) {
        /* Closing the handle is how a WinINet read in progress is ended. */
        inet.InternetCloseHandle(conn->request);
    }
}

void plat_net_close(PlatConn *conn)
{
    if (conn->fd != INVALID_SOCKET) {
        sock.closesocket(conn->fd);
    } else {
        if (conn->request && !InterlockedExchange(&conn->aborted, 1))
            inet.InternetCloseHandle(conn->request);
        if (conn->session)
            inet.InternetCloseHandle(conn->session);
    }
    free(conn);
}

/* --- Threads ----------------------------------------------------------------- */

typedef struct {
    void (*fn)(void *);
    void *arg;
} ThreadStart;

static DWORD WINAPI thread_entry(LPVOID opaque)
{
    ThreadStart start = *(ThreadStart *)opaque;

    free(opaque);
    start.fn(start.arg);
    return 0;
}

int plat_thread_start(void (*fn)(void *), void *arg)
{
    ThreadStart *start = malloc(sizeof *start);
    HANDLE thread;
    DWORD id;

    if (!start)
        return 0;
    start->fn = fn;
    start->arg = arg;
    thread = CreateThread(NULL, 0, thread_entry, start, 0, &id);
    if (!thread) {
        free(start);
        return 0;
    }
    CloseHandle(thread);    /* it runs on; nobody waits for it */
    return 1;
}

void plat_sleep_ms(int ms)
{
    Sleep((DWORD)ms);
}
