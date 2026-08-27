#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <boring/pty.h>

static uint64_t woke_pid;
static size_t checks;
bool task_wake_pid(uint64_t pid);
bool task_wake_pid(uint64_t pid) { woke_pid = pid; return true; }

static int check(bool ok, const char *name) {
    ++checks;
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); return 1; }
    return 0;
}

int main(void) {
    struct pty_handle m, s, m2, s2, stale;
    struct pty_poll_state state;
    struct pty_stats stats;
    struct pty_handle masters[KERNEL_PTY_MAX], slaves[KERNEL_PTY_MAX];
    char out[8192];
    char fill[KERNEL_PTY_RING_CAPACITY + 16U];
    size_t n;
    size_t i;
    int failed = 0;
    memset(fill, 'x', sizeof(fill));
    failed |= check(pty_init(), "init");
    failed |= check(pty_create_pair(&m, &s) == PTY_RESULT_OK, "create");
    failed |= check(pty_write(m, "abc", 3U, &n) == PTY_RESULT_OK && n == 3U, "m->s write");
    memset(out, 0, sizeof(out));
    failed |= check(pty_read(s, out, 3U, &n) == PTY_RESULT_OK && n == 3U && memcmp(out,"abc",3U)==0, "m->s read");
    failed |= check(pty_write(s, "xyz", 3U, &n) == PTY_RESULT_OK && n == 3U, "s->m write");
    failed |= check(pty_read(m, out, 3U, &n) == PTY_RESULT_OK && n == 3U && memcmp(out,"xyz",3U)==0, "s->m read");
    failed |= check(pty_read(m, out, 1U, &n) == PTY_RESULT_WOULD_BLOCK && n == 0U, "empty would block");
    failed |= check(pty_arm_read_waiter(m, 42ULL) == PTY_RESULT_OK, "arm waiter");
    woke_pid = 0ULL;
    failed |= check(pty_write(s, "q", 1U, &n) == PTY_RESULT_OK && woke_pid == 42ULL, "wake read");
    failed |= check(pty_read(m, out, 1U, &n) == PTY_RESULT_OK && out[0]=='q', "wake byte");
    failed |= check(pty_write(m, fill, sizeof(fill), &n) == PTY_RESULT_OK && n == KERNEL_PTY_RING_CAPACITY, "full partial");
    failed |= check(pty_write(m, fill, 1U, &n) == PTY_RESULT_WOULD_BLOCK && n == 0U, "full would block");
    failed |= check(pty_read(s, out, 17U, &n) == PTY_RESULT_OK && n == 17U, "partial drain");
    failed |= check(pty_write(m, fill, 17U, &n) == PTY_RESULT_OK && n == 17U, "wrap write");
    for (i=0U;i<KERNEL_PTY_RING_CAPACITY;i+=sizeof(out)) {
        size_t want=KERNEL_PTY_RING_CAPACITY-i; if (want>sizeof(out)) want=sizeof(out);
        failed |= check(pty_read(s,out,want,&n)==PTY_RESULT_OK && n==want,"wrap drain");
    }
    failed |= check(pty_create_pair(&m2, &s2) == PTY_RESULT_OK, "second pty");
    failed |= check(pty_write(m, "A",1U,&n)==PTY_RESULT_OK, "isolation write A");
    failed |= check(pty_read(s2,out,1U,&n)==PTY_RESULT_WOULD_BLOCK, "isolation no cross");
    failed |= check(pty_poll(s,&state)==PTY_RESULT_OK && state.readable && !state.hup, "poll readable");
    failed |= check(pty_read(s,out,1U,&n)==PTY_RESULT_OK && out[0]=='A', "isolation own read");
    stale=m2;
    failed |= check(pty_close(m2)==PTY_RESULT_OK && pty_close(s2)==PTY_RESULT_OK, "close second");
    failed |= check(pty_create_pair(&m2,&s2)==PTY_RESULT_OK, "reuse slot");
    failed |= check(pty_read(stale,out,1U,&n)==PTY_RESULT_INVALID, "stale handle");
    failed |= check(pty_retain(s)==PTY_RESULT_OK, "retain slave");
    failed |= check(pty_close(s)==PTY_RESULT_OK, "drop one slave ref");
    failed |= check(pty_close(m)==PTY_RESULT_OK, "close master");
    failed |= check(pty_poll(s,&state)==PTY_RESULT_OK && state.hup, "hup poll");
    failed |= check(pty_read(s,out,1U,&n)==PTY_RESULT_HUP && n==0U, "eof hup");
    failed |= check(pty_write(s,"z",1U,&n)==PTY_RESULT_HUP, "write peer closed");
    failed |= check(pty_close(s)==PTY_RESULT_OK, "final slave close");
    failed |= check(pty_close(m2)==PTY_RESULT_OK && pty_close(s2)==PTY_RESULT_OK, "final second close");
    failed |= check(pty_get_stats(&stats) && stats.active_pairs == 0U &&
                    stats.references == 0ULL && stats.read_waiters == 0U &&
                    stats.queued_bytes == 0U, "all original resources released");
    for (i = 0U; i < KERNEL_PTY_MAX; ++i) {
        failed |= check(pty_create_pair(&masters[i], &slaves[i]) == PTY_RESULT_OK,
                        "allocate every bounded PTY slot");
        failed |= check(pty_arm_read_waiter(slaves[i], 100ULL + i) == PTY_RESULT_OK,
                        "arm each slave before peer exit");
        failed |= check(pty_write(slaves[i], "pending", 7U, &n) == PTY_RESULT_OK && n == 7U,
                        "queue output before peer exit");
    }
    failed |= check(pty_create_pair(&m, &s) == PTY_RESULT_NO_SPACE,
                    "ninth pair rejected without overwriting live slots");
    failed |= check(pty_get_stats(&stats) && stats.active_pairs == KERNEL_PTY_MAX &&
                    stats.references == 2ULL * KERNEL_PTY_MAX &&
                    stats.read_waiters == KERNEL_PTY_MAX &&
                    stats.queued_bytes == 7U * KERNEL_PTY_MAX, "full bounded resource accounting");
    stale = masters[0];
    for (i = 0U; i < KERNEL_PTY_MAX; ++i) {
        woke_pid = 0ULL;
        failed |= check(pty_close(masters[i]) == PTY_RESULT_OK && woke_pid == 100ULL + i,
                        "master exit wakes slave waiter");
        failed |= check(pty_close(slaves[i]) == PTY_RESULT_OK, "slave exit releases final reference");
    }
    failed |= check(pty_get_stats(&stats) && stats.active_pairs == 0U &&
                    stats.references == 0ULL && stats.read_waiters == 0U &&
                    stats.queued_bytes == 0U, "exhaustion and HUP leave no resource behind");
    failed |= check(pty_create_pair(&m, &s) == PTY_RESULT_OK && m.slot == stale.slot &&
                    m.generation != stale.generation, "exhausted slot reused with new generation");
    failed |= check(pty_retain(stale) == PTY_RESULT_INVALID &&
                    pty_close(stale) == PTY_RESULT_INVALID, "stale endpoint cannot affect reused pair");
    failed |= check(pty_close(m) == PTY_RESULT_OK && pty_close(s) == PTY_RESULT_OK,
                    "reused slot released");
    if (!failed) printf("PTY host test passed: %zu checks.\n", checks);
    return failed ? 1 : 0;
}
