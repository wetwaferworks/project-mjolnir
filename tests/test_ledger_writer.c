/* The ledger's writer token, torn-line repair and tamper checks.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_ledger.h"
#include "mj_test.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static mj_budget B;
static char LEDGER[MJ_PATH_MAX];

static void rec(mj_dream_record *r, const char *id) {
    mj_ledger_record_init(r);
    snprintf(r->dream_id, sizeof r->dream_id, "%s", id);
    snprintf(r->when, sizeof r->when, "2026-11-19T09:00:00Z");
    snprintf(r->grader, sizeof r->grader, "auto-maze");
    snprintf(r->outcome, sizeof r->outcome, "dry-run-pass");
}

static size_t load(const char *path, char *buf, size_t cap) {
    uint8_t *b;
    size_t n;
    if (mj_fs_read_all(path, cap, &B, &b, &n) != MJ_OK) return 0;
    memcpy(buf, b, n);
    mj_bfree(&B, b, n ? n : 1);
    return n;
}

static void put(const char *path, const char *buf, size_t n) {
    (void)mj_fs_remove(path);
    (void)mj_fs_write_atomic(path, buf, n, 0);
}

static uint64_t count_of(const char *path) {
    mj_ledger_state st;
    uint64_t bad;
    mj_budget b;
    mj_budget_init(&b, 128u << 20);
    if (mj_ledger_verify_file(path, &b, &st, &bad) != MJ_OK) return (uint64_t)-1;
    return st.count;
}

/* remove the files one test leaves next to the ledger */
typedef struct sweep { const char *dir; const char *prefix; } sweep;
static int sweep_cb(const char *name, void *u) {
    sweep *s = (sweep *)u;
    char p[MJ_PATH_MAX];
    if (strncmp(name, s->prefix, strlen(s->prefix)) == 0 && mj_path_join(p, sizeof p, s->dir, name) == MJ_OK)
        (void)mj_fs_remove(p);
    return 0;
}
static void clean(void) {
    char dir[MJ_PATH_MAX];
    sweep s;
    mj_t_dir(dir, sizeof dir, "");
    if (dir[strlen(dir) - 1] == '/') dir[strlen(dir) - 1] = '\0';
    s.dir = dir;
    s.prefix = "writer_test.mjl";
    (void)mj_fs_list(dir, 100000, sweep_cb, &s);
}

/* ---- many writers at once: every success lands, the chain stays whole ---- */
#define NWORKERS 6
#define PER_WORKER 25
static volatile long ok_total, busy_total;

static void worker_body(int id) {
    int i, done = 0, tries = 0;
    mj_budget b;
    mj_budget_init(&b, 128u << 20);
    for (i = 0; done < PER_WORKER && tries < 200000; ++i, ++tries) {
        mj_dream_record r;
        char nm[32];
        mj_status s;
        snprintf(nm, sizeof nm, "w%d-%d", id, done);
        rec(&r, nm);
        {
            /* the same steps as mj_ledger_append, without its refusal line on every busy try */
            mj_ledger_writer w;
            s = mj_ledger_writer_claim(LEDGER, &w, NULL);
            if (s == MJ_OK) {
                mj_status rs;
                s = mj_ledger_append_held(&w, &b, &r);
                rs = mj_ledger_writer_release(&w);
                if (s == MJ_OK) s = rs;
            }
        }
        if (s == MJ_OK) {
            ++done;
#if defined(_WIN32)
            InterlockedIncrement(&ok_total);
#else
            __atomic_add_fetch(&ok_total, 1, __ATOMIC_RELAXED);
#endif
        } else if (s == MJ_E_BUSY) {
#if defined(_WIN32)
            InterlockedIncrement(&busy_total);
#else
            __atomic_add_fetch(&busy_total, 1, __ATOMIC_RELAXED);
#endif
        } else {
            fprintf(stderr, "worker %d: unexpected %s\n", id, mj_status_name(s));
            return;
        }
    }
}

#if defined(_WIN32)
static DWORD WINAPI worker(LPVOID p) { worker_body((int)(intptr_t)p); return 0; }
#else
static void *worker(void *p) { worker_body((int)(intptr_t)p); return NULL; }
#endif

int main(void) {
    mj_ledger_writer w1, w2;
    mj_ledger_holder h;
    mj_dream_record r;
    char buf[65536], buf2[65536], tok[MJ_PATH_MAX], moved[MJ_PATH_MAX], bak[MJ_PATH_MAX];
    size_t n, n2, removed;
    mj_ledger_state st;
    uint64_t bad, pid, start;
    int i;

    mj_budget_init(&B, 128u << 20);
    mj_t_dir(LEDGER, sizeof LEDGER, "writer_test.mjl");
    clean();
    snprintf(tok, sizeof tok, "%s.writer", LEDGER);

    /* ---- a second writer is refused, never waits, never steals ---- */
    CHECK_ST(mj_ledger_writer_claim(LEDGER, &w1, NULL), MJ_OK);
    CHECK(mj_fs_exists(tok));
    CHECK_ST(mj_ledger_writer_claim(LEDGER, &w2, &h), MJ_E_BUSY);
    mj_proc_self(&pid, &start);
    CHECK_EQ(h.pid, pid);
    CHECK_EQ(h.running, 1);
    rec(&r, "second-writer");
    fprintf(stderr, "(the next refusal line is expected)\n");
    CHECK_ST(mj_ledger_append(LEDGER, &B, &r), MJ_E_BUSY);
    CHECK_EQ(count_of(LEDGER), 0u);
    /* a live holder is never cleared as stale */
    CHECK_ST(mj_ledger_writer_release_stale(LEDGER, &h, moved, sizeof moved), MJ_E_BUSY);
    CHECK(mj_fs_exists(tok));
    /* the holder writes while it holds the token */
    for (i = 0; i < 3; ++i) {
        rec(&r, "held");
        CHECK_ST(mj_ledger_append_held(&w1, &B, &r), MJ_OK);
    }
    CHECK_ST(mj_ledger_writer_release(&w1), MJ_OK);
    CHECK(!mj_fs_exists(tok));
    rec(&r, "after-release");
    CHECK_ST(mj_ledger_append(LEDGER, &B, &r), MJ_OK);
    CHECK_EQ(r.seq, 4u);
    CHECK(!mj_fs_exists(tok)); /* append leaves no token behind */

    /* releasing a token that is no longer ours leaves it alone */
    CHECK_ST(mj_ledger_writer_claim(LEDGER, &w1, NULL), MJ_OK);
    {
        static const char other[] = "MJW1|1|1|elsewhere|2026-11-19T09:00:00Z|0123456789abcdef0123456789abcdef\n";
        put(tok, other, sizeof other - 1);
    }
    CHECK_ST(mj_ledger_writer_release(&w1), MJ_E_FORBIDDEN);
    CHECK(mj_fs_exists(tok));
    /* ... and a token written on another host is never cleared from here */
    CHECK_ST(mj_ledger_writer_release_stale(LEDGER, &h, moved, sizeof moved), MJ_E_FORBIDDEN);
    CHECK(mj_fs_exists(tok));
    /* an unreadable token counts as held and is not cleared automatically */
    put(tok, "garbage", 7);
    CHECK_ST(mj_ledger_writer_claim(LEDGER, &w2, &h), MJ_E_BUSY);
    CHECK_ST(mj_ledger_writer_release_stale(LEDGER, &h, moved, sizeof moved), MJ_E_FORBIDDEN);
    CHECK(mj_fs_exists(tok));

    /* ---- a stale token from a process that is gone ---- */
    {
        char host[64], t[256];
        uint64_t dead = 0;
        int tl;
        mj_host_name(host, sizeof host);
#if defined(_WIN32)
        {
            STARTUPINFOW si;
            PROCESS_INFORMATION pi;
            wchar_t cmd[] = L"cmd.exe /c exit 0";
            memset(&si, 0, sizeof si);
            si.cb = sizeof si;
            if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 10000);
                dead = pi.dwProcessId;
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess); /* last handle: the id is released */
            }
        }
#else
        {
            pid_t c = fork();
            if (c == 0) _exit(0);
            if (c > 0) { (void)waitpid(c, NULL, 0); dead = (uint64_t)c; }
        }
#endif
        CHECK(dead != 0);
        tl = snprintf(t, sizeof t, "MJW1|%llu|0|%s|2026-11-19T09:00:00Z|00000000000000000000000000000000\n",
                      (unsigned long long)dead, host);
        put(tok, t, (size_t)tl);
        CHECK_ST(mj_ledger_writer_claim(LEDGER, &w2, &h), MJ_E_BUSY); /* still refused: no silent takeover */
        CHECK_EQ(h.running, 0);
        CHECK_ST(mj_ledger_writer_release_stale(LEDGER, &h, moved, sizeof moved), MJ_OK);
        CHECK(!mj_fs_exists(tok));
        CHECK(mj_fs_exists(moved)); /* kept for the record */
        CHECK_EQ(load(moved, buf, sizeof buf), (size_t)tl);
        /* our own id with a different start stamp is a reused id: stale too */
        tl = snprintf(t, sizeof t, "MJW1|%llu|%llu|%s|2026-11-19T09:00:01Z|11111111111111111111111111111111\n",
                      (unsigned long long)pid, (unsigned long long)(start + 12345u), host);
        put(tok, t, (size_t)tl);
        if (start != 0) CHECK_ST(mj_ledger_writer_release_stale(LEDGER, &h, moved, sizeof moved), MJ_OK);
        else (void)mj_fs_remove(tok);
        CHECK(!mj_fs_exists(tok));
        CHECK_ST(mj_ledger_writer_release_stale(LEDGER, &h, moved, sizeof moved), MJ_E_NOTFOUND);
    }

    /* ---- many writers at once ---- */
    {
#if defined(_WIN32)
        HANDLE th[NWORKERS];
        for (i = 0; i < NWORKERS; ++i) th[i] = CreateThread(NULL, 0, worker, (LPVOID)(intptr_t)i, 0, NULL);
        WaitForMultipleObjects(NWORKERS, th, TRUE, INFINITE);
        for (i = 0; i < NWORKERS; ++i) CloseHandle(th[i]);
#else
        pthread_t th[NWORKERS];
        for (i = 0; i < NWORKERS; ++i) pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i);
        for (i = 0; i < NWORKERS; ++i) pthread_join(th[i], NULL);
#endif
        CHECK_EQ(ok_total, (long)(NWORKERS * PER_WORKER));
        CHECK_EQ(count_of(LEDGER), 4u + NWORKERS * PER_WORKER);
        CHECK(!mj_fs_exists(tok));
        printf("concurrent writers: %ld appends, %ld refused as busy, chain whole\n", ok_total, busy_total);
    }
#if !defined(_WIN32)
    /* the same across processes */
    {
        pid_t kids[4];
        int status, k, all_ok = 1;
        uint64_t before = count_of(LEDGER);
        for (k = 0; k < 4; ++k) {
            kids[k] = fork();
            if (kids[k] == 0) {
                ok_total = 0;
                worker_body(100 + k);
                _exit(ok_total == PER_WORKER ? 0 : 1);
            }
        }
        for (k = 0; k < 4; ++k) {
            if (kids[k] < 0 || waitpid(kids[k], &status, 0) != kids[k] || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
                all_ok = 0;
        }
        CHECK(all_ok);
        CHECK_EQ(count_of(LEDGER), before + 4u * PER_WORKER);
    }
#endif

    /* ---- a torn last line: appends refuse, repair cuts only that line ---- */
    n = load(LEDGER, buf, sizeof buf);
    CHECK(n > 0);
    {
        const char tail[] = "MJL1|999|00ab";
        memcpy(buf2, buf, n);
        memcpy(buf2 + n, tail, sizeof tail - 1);
        n2 = n + sizeof tail - 1;
        put(LEDGER, buf2, n2);
    }
    rec(&r, "on-torn");
    CHECK_ST(mj_ledger_append(LEDGER, &B, &r), MJ_E_CHAIN);
    CHECK_EQ(load(LEDGER, buf2, sizeof buf2), n2); /* nothing appended */
    CHECK_ST(mj_ledger_check_tail(buf2, n2, &st, &bad, &removed, &i), MJ_OK);
    CHECK_EQ(removed, n);
    CHECK_EQ(i, 1);
    /* repair needs the token */
    memset(&w1, 0, sizeof w1);
    CHECK_ST(mj_ledger_repair_tail(&w1, &B, &removed, bak, sizeof bak), MJ_E_INVAL);
    CHECK_ST(mj_ledger_writer_claim(LEDGER, &w1, NULL), MJ_OK);
    CHECK_ST(mj_ledger_repair_tail(&w1, &B, &removed, bak, sizeof bak), MJ_OK);
    CHECK_EQ(removed, n2 - n);
    CHECK_EQ(load(LEDGER, buf2, sizeof buf2), n);
    CHECK(memcmp(buf2, buf, n) == 0); /* every complete record untouched */
    CHECK(mj_fs_exists(bak));
    CHECK_EQ(load(bak, buf2, sizeof buf2), n2); /* the backup is the whole torn file */
    /* repairing an intact ledger does nothing */
    CHECK_ST(mj_ledger_repair_tail(&w1, &B, &removed, bak, sizeof bak), MJ_OK);
    CHECK_EQ(removed, 0u);
    CHECK_ST(mj_ledger_writer_release(&w1), MJ_OK);
    rec(&r, "after-repair");
    CHECK_ST(mj_ledger_append(LEDGER, &B, &r), MJ_OK);
    n = load(LEDGER, buf, sizeof buf);

    /* ---- repair refuses when history is wrong, and never rewrites it ---- */
    {
        size_t nl1 = 0;
        while (buf[nl1] != '\n') ++nl1;
        memcpy(buf2, buf, n);
        buf2[nl1 + 30] ^= 1;               /* tamper with record 2 */
        memcpy(buf2 + n, "MJL1|", 5);       /* and leave a torn tail */
        put(LEDGER, buf2, n + 5);
        CHECK_ST(mj_ledger_writer_claim(LEDGER, &w1, NULL), MJ_OK);
        CHECK_ST(mj_ledger_repair_tail(&w1, &B, &removed, bak, sizeof bak), MJ_E_CHAIN);
        CHECK_EQ(load(LEDGER, buf, sizeof buf), n + 5);
        CHECK(memcmp(buf, buf2, n + 5) == 0); /* file unchanged */
        /* tamper is still detected by the plain verifier, at the right line */
        CHECK_ST(mj_ledger_verify_buf(buf2, n, &st, &bad), MJ_E_CHAIN);
        CHECK_EQ(bad, 2u);
        /* a tail that is not the start of a record is not "torn": refuse */
        buf2[nl1 + 30] ^= 1;
        memcpy(buf2 + n, "hello", 5);
        put(LEDGER, buf2, n + 5);
        CHECK_ST(mj_ledger_repair_tail(&w1, &B, &removed, bak, sizeof bak), MJ_E_CORRUPT);
        CHECK_EQ(load(LEDGER, buf, sizeof buf), n + 5);
        /* a cut through the middle of a record is still a torn tail */
        memcpy(buf2 + n, "MJL1|1000|", 10);
        put(LEDGER, buf2, n + 10);
        CHECK_ST(mj_ledger_repair_tail(&w1, &B, &removed, bak, sizeof bak), MJ_OK);
        CHECK_EQ(removed, 10u);
        CHECK_ST(mj_ledger_writer_release(&w1), MJ_OK);
    }
    CHECK(count_of(LEDGER) != (uint64_t)-1);

    clean();
    CHECK_EQ(B.used, 0u);
    return mj_t_done("test_ledger_writer");
}
