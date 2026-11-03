/* Stub out rseq (syscall 293) - kernel 3.18.35 doesn't support it.
 * glibc 2.41 tries to call it during thread init, causing SIGSEGV. */
#define _GNU_SOURCE
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

/* rseq() wrapper - return ENOSYS */
long rseq(volatile struct rseq *rseq_abi, uint32_t rseq_len, int flags, uint32_t sig) {
    (void)rseq_abi; (void)rseq_len; (void)flags; (void)sig;
    errno = ENOSYS;
    return -1;
}

/* Override syscall() to intercept rseq */
long syscall(long n, ...) {
    if (n == 293) {  /* rseq */
        errno = ENOSYS;
        return -1;
    }
    /* For other syscalls, we can't easily forward varargs, return ENOSYS */
    errno = ENOSYS;
    return -1;
}
