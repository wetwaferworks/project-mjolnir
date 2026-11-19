/* Mjolnir: the labeling page's listening socket (see mj_labeler.h).
 *
 * One thread, one connection at a time: read a whole request (or give up
 * after a few seconds or past the size limits), answer it, close. Bound to
 * 127.0.0.1 only.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#if !defined(_WIN32)
#define _XOPEN_SOURCE 700 /* gmtime_r, sockets */
#endif
#include "mjolnir/mj_labeler.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
static void sock_close(sock_t s) { closesocket(s); }
#define SHUT_SEND SD_SEND
static void set_timeout(sock_t s, int ms) {
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&t, sizeof t);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
static void sock_close(sock_t s) { close(s); }
#define SHUT_SEND SHUT_WR
static void set_timeout(sock_t s, int ms) {
    struct timeval t;
    t.tv_sec = ms / 1000;
    t.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof t);
}
#endif

#define CONN_TIMEOUT_MS 5000
#define DRAIN_MAX (64 * 1024)

static void serve_one(mj_lab *s, sock_t c) {
    static char buf[MJ_LAB_MAX_HEAD + MJ_LAB_MAX_BODY + 1];
    size_t n = 0, total = 0, sent = 0;
    int state = 0;
    mj_jout resp;
    set_timeout(c, CONN_TIMEOUT_MS);
    while (n < sizeof buf - 1) {
        int got = (int)recv(c, buf + n, (int)(sizeof buf - 1 - n), 0);
        if (got <= 0) return; /* closed, timed out or failed: drop it */
        n += (size_t)got;
        state = mj_lab_http_complete(buf, n, &total);
        if (state != 0) break;
    }
    if (state == 0) total = n; /* full buffer without a whole request: answered as too large */
    mj_jout_init(&resp, (size_t)8 << 20);
    mj_lab_http(s, buf, state == 1 ? total : n, &resp);
    while (!resp.failed && sent < resp.n) {
        int w = (int)send(c, resp.p + sent, (int)(resp.n - sent), 0);
        if (w <= 0) break;
        sent += (size_t)w;
    }
    mj_jout_free(&resp);
    /* Closing with unread request bytes makes the system reset the
     * connection, and the client may lose the answer it was sent (a refused
     * request that is too large). So end the sending side, then read what is
     * left, up to a limit, before the caller closes. */
    shutdown(c, SHUT_SEND);
    for (sent = 0; sent < DRAIN_MAX;) {
        int got = (int)recv(c, buf, (int)(sizeof buf - 1), 0);
        if (got <= 0) break;
        sent += (size_t)got;
    }
}

mj_status mj_lab_serve(mj_lab *s, int open_browser) {
    struct sockaddr_in a;
    sock_t l;
    int one = 1;
    char url[64];
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return MJ_E_IO;
#else
    signal(SIGPIPE, SIG_IGN); /* a closed browser tab must not end the server */
#endif
    l = socket(AF_INET, SOCK_STREAM, 0);
    if (l == BAD_SOCK) return MJ_E_IO;
#if defined(_WIN32)
    setsockopt(l, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&one, sizeof one); /* no one else may share the port */
#else
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(s->port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(l, (struct sockaddr *)&a, sizeof a) != 0 || listen(l, 8) != 0) {
        sock_close(l);
        return MJ_E_IO;
    }
    snprintf(url, sizeof url, "http://127.0.0.1:%u/", (unsigned)s->port);
    printf("Mjolnir labeling is open at %s  (close this window to stop)\n", url);
    fflush(stdout);
#if defined(_WIN32)
    if (open_browser) ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
    (void)open_browser; /* open the address by hand */
#endif
    for (;;) {
        sock_t c = accept(l, NULL, NULL);
        if (c == BAD_SOCK) continue;
        serve_one(s, c);
        sock_close(c);
    }
}
