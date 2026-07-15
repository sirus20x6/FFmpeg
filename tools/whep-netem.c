/* Test-only LD_PRELOAD shim for deterministic WHEP egress impairment.
 *
 * Build/use is automated by whep-sfu-test.sh when either cadence variable is
 * set.  ICE, DTLS, TCP, RTCP, and the first 200 RTP sends are untouched.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

typedef ssize_t (*send_fn)(int, const void *, size_t, int);

static _Atomic unsigned media_seen;
static _Atomic unsigned media_dropped;
static _Atomic unsigned media_delayed;

static unsigned cadence(const char *name)
{
    const char *text = getenv(name);
    char *end = NULL;
    unsigned long value;

    if (!text || !*text)
        return 0;
    value = strtoul(text, &end, 10);
    return end && !*end && value <= UINT32_MAX ? (unsigned)value : 0;
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
    static send_fn real_send;
    const uint8_t *packet = buf;
    int type = 0;
    socklen_t type_len = sizeof(type);

    if (!real_send)
        real_send = (send_fn)dlsym(RTLD_NEXT, "send");
    if (!real_send) {
        errno = EIO;
        return -1;
    }
    if (len >= 12 &&
        !getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_len) &&
        type == SOCK_DGRAM && (packet[0] & 0xc0) == 0x80 &&
        (packet[1] & 0x7f) >= 96) {
        unsigned n = atomic_fetch_add_explicit(&media_seen, 1,
                                               memory_order_relaxed) + 1;
        unsigned drop_every = cadence("WHEP_NETEM_DROP_EVERY");
        unsigned delay_every = cadence("WHEP_NETEM_DELAY_EVERY");

        if (n > 200 && drop_every && n % drop_every == 0) {
            atomic_fetch_add_explicit(&media_dropped, 1,
                                      memory_order_relaxed);
            return (ssize_t)len;
        }
        if (n > 200 && delay_every && n % delay_every == 0) {
            const struct timespec delay = { .tv_sec = 0,
                                            .tv_nsec = 4 * 1000 * 1000 };
            nanosleep(&delay, NULL);
            atomic_fetch_add_explicit(&media_delayed, 1,
                                      memory_order_relaxed);
        }
    }
    return real_send(fd, buf, len, flags);
}

__attribute__((destructor))
static void report_impairment(void)
{
    fprintf(stderr,
            "memepipe-netem: media_seen=%u dropped=%u delayed=%u\n",
            atomic_load_explicit(&media_seen, memory_order_relaxed),
            atomic_load_explicit(&media_dropped, memory_order_relaxed),
            atomic_load_explicit(&media_delayed, memory_order_relaxed));
}
