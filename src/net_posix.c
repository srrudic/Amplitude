/* Network connections and threads for Linux and other POSIX systems (see
 * platform.h). Plain connections use sockets directly. Secure ones use the
 * system's OpenSSL, which is looked up when the first secure connection is
 * made: it is neither linked nor needed to build, and without it only
 * secure addresses fail. */
#include "platform.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define CONNECT_TIMEOUT_S 5     /* for each of a server's addresses */
#define NET_TIMEOUT_S     15    /* the longest a send or recv may stall */

struct PlatConn {
    int fd;
    void *ssl;      /* SSL *, or NULL for a plain connection */
};

/* --- OpenSSL, by hand -------------------------------------------------------- */

enum { SSL_VERIFY_PEER_ = 1, SSL_CTRL_SET_TLSEXT_HOSTNAME_ = 55, TLSEXT_NAMETYPE_host_name_ = 0 };

static struct {
    const void *(*TLS_client_method)(void);
    void *(*SSL_CTX_new)(const void *method);
    int (*SSL_CTX_set_default_verify_paths)(void *ctx);
    void (*SSL_CTX_set_verify)(void *ctx, int mode, void *callback);
    void *(*SSL_new)(void *ctx);
    int (*SSL_set_fd)(void *ssl, int fd);
    long (*SSL_ctrl)(void *ssl, int cmd, long larg, void *parg);
    int (*SSL_set1_host)(void *ssl, const char *host);
    int (*SSL_connect)(void *ssl);
    int (*SSL_read)(void *ssl, void *buffer, int size);
    int (*SSL_write)(void *ssl, const void *data, int size);
    void (*SSL_free)(void *ssl);
} tls;
static void *tls_context;       /* SSL_CTX *, shared by all connections */
static pthread_mutex_t tls_lock = PTHREAD_MUTEX_INITIALIZER;

static const struct { const char *name; void *slot; } tls_symbols[] = {
    { "TLS_client_method", &tls.TLS_client_method },
    { "SSL_CTX_new", &tls.SSL_CTX_new },
    { "SSL_CTX_set_default_verify_paths", &tls.SSL_CTX_set_default_verify_paths },
    { "SSL_CTX_set_verify", &tls.SSL_CTX_set_verify },
    { "SSL_new", &tls.SSL_new },
    { "SSL_set_fd", &tls.SSL_set_fd },
    { "SSL_ctrl", &tls.SSL_ctrl },
    { "SSL_set1_host", &tls.SSL_set1_host },
    { "SSL_connect", &tls.SSL_connect },
    { "SSL_read", &tls.SSL_read },
    { "SSL_write", &tls.SSL_write },
    { "SSL_free", &tls.SSL_free },
};

/* Loads the library and prepares a context that checks certificates against
 * the system's list. Returns 0 if there is no usable OpenSSL. */
static int tls_ready(void)
{
    static const char *const names[] = { "libssl.so.3", "libssl.so.1.1" };
    static int tried;
    void *library = NULL;
    size_t i;

    pthread_mutex_lock(&tls_lock);
    if (!tried) {
        tried = 1;
        /* OpenSSL writes with plain write(), so a connection the other side
         * has closed would end the whole program with SIGPIPE. Ignored, the
         * write just fails. (Our own sends pass MSG_NOSIGNAL instead.) */
        signal(SIGPIPE, SIG_IGN);
        for (i = 0; i < sizeof names / sizeof names[0] && !library; i++)
            library = dlopen(names[i], RTLD_NOW);
        for (i = 0; library && i < sizeof tls_symbols / sizeof tls_symbols[0]; i++) {
            void *address = dlsym(library, tls_symbols[i].name);

            if (!address)
                library = NULL;
            else
                memcpy(tls_symbols[i].slot, &address, sizeof address);
        }
        if (library) {
            tls_context = tls.SSL_CTX_new(tls.TLS_client_method());
            if (tls_context) {
                tls.SSL_CTX_set_default_verify_paths(tls_context);
                tls.SSL_CTX_set_verify(tls_context, SSL_VERIFY_PEER_, NULL);
            }
        }
    }
    pthread_mutex_unlock(&tls_lock);
    return tls_context != NULL;
}

/* --- Connections ------------------------------------------------------------- */

/* connect() with a limit of its own: left to itself it waits a quarter of
 * a minute or more on an address that does not answer. */
static int connect_within(int fd, const struct sockaddr *address, socklen_t length, int seconds)
{
    struct pollfd wait = { fd, POLLOUT, 0 };
    int flags = fcntl(fd, F_GETFL, 0), error = 0, ready;
    socklen_t size = sizeof error;

    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (connect(fd, address, length) != 0) {
        if (errno != EINPROGRESS)
            return 0;
        do
            ready = poll(&wait, 1, seconds * 1000);
        while (ready < 0 && errno == EINTR);
        if (ready != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error)
            return 0;
    }
    fcntl(fd, F_SETFL, flags);
    return 1;
}

/* The secure greeting on a connected socket. */
static int tls_begin(PlatConn *conn, const char *host)
{
    conn->ssl = tls.SSL_new(tls_context);
    return conn->ssl && tls.SSL_set_fd(conn->ssl, conn->fd) &&
           /* the name the server must present a certificate for, and the one sent in the greeting */
           tls.SSL_set1_host(conn->ssl, host) &&
           tls.SSL_ctrl(conn->ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME_, TLSEXT_NAMETYPE_host_name_, (void *)host) &&
           tls.SSL_connect(conn->ssl) == 1;
}

PlatConn *plat_net_connect(const char *host, int port, int secure)
{
    struct addrinfo hints, *list = NULL, *a;
    struct timeval timeout = { NET_TIMEOUT_S, 0 };
    char service[16];
    PlatConn *conn = NULL;

    if (secure && !tls_ready())
        return NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof service, "%d", port);
    if (getaddrinfo(host, service, &hints, &list) != 0)
        return NULL;
    /* A name often stands for several servers, and one of them may be down
     * or turn the greeting away: each address is tried in turn until one
     * works all the way. */
    for (a = list; a && !conn; a = a->ai_next) {
        int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);

        if (fd < 0)
            continue;
        /* A dead server must not hold a thread for ever. */
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        if (connect_within(fd, a->ai_addr, a->ai_addrlen, CONNECT_TIMEOUT_S))
            conn = calloc(1, sizeof *conn);
        if (!conn) {
            close(fd);
            continue;
        }
        conn->fd = fd;
        if (secure && !tls_begin(conn, host)) {
            plat_net_close(conn);
            conn = NULL;
        }
    }
    freeaddrinfo(list);
    return conn;
}

PlatConn *plat_https_get(const char *url, const char *request_headers, char *response, size_t response_size)
{
    (void)url;
    (void)request_headers;
    (void)response;
    (void)response_size;
    return NULL;        /* not needed here: plat_net_connect() does secure connections */
}

int plat_net_send(PlatConn *conn, const void *data, int size)
{
    const char *p = data;

    while (size > 0) {
        int sent = conn->ssl ? tls.SSL_write(conn->ssl, p, size) : (int)send(conn->fd, p, (size_t)size, MSG_NOSIGNAL);

        if (sent <= 0)
            return 0;
        p += sent;
        size -= sent;
    }
    return 1;
}

int plat_net_recv(PlatConn *conn, void *buffer, int size)
{
    return conn->ssl ? tls.SSL_read(conn->ssl, buffer, size) : (int)recv(conn->fd, buffer, (size_t)size, 0);
}

void plat_net_abort(PlatConn *conn)
{
    shutdown(conn->fd, SHUT_RDWR);
}

void plat_net_close(PlatConn *conn)
{
    if (conn->ssl)
        tls.SSL_free(conn->ssl);
    close(conn->fd);
    free(conn);
}

/* --- Threads ----------------------------------------------------------------- */

typedef struct {
    void (*fn)(void *);
    void *arg;
} ThreadStart;

static void *thread_entry(void *opaque)
{
    ThreadStart start = *(ThreadStart *)opaque;

    free(opaque);
    start.fn(start.arg);
    return NULL;
}

int plat_thread_start(void (*fn)(void *), void *arg)
{
    ThreadStart *start = malloc(sizeof *start);
    pthread_t thread;

    if (!start)
        return 0;
    start->fn = fn;
    start->arg = arg;
    if (pthread_create(&thread, NULL, thread_entry, start) != 0) {
        free(start);
        return 0;
    }
    pthread_detach(thread);
    return 1;
}

void plat_sleep_ms(int ms)
{
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };

    nanosleep(&t, NULL);
}
