/*
 * Private, generation-fenced control events for MemePipe's playout/WHEP
 * process.  Human-readable av_log output is deliberately not a controller
 * protocol: FFmpeg may add, remove, or rewrite component prefixes at any
 * time.  The stager passes one inherited pipe descriptor to both components
 * and consumes only these versioned records.
 */
#ifndef AVFORMAT_MEMEPIPE_EVENT_H
#define AVFORMAT_MEMEPIPE_EVENT_H

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libavutil/error.h"

#define MEMEPIPE_EVENT_PREFIX "MEMEPIPE/1\t"
#define MEMEPIPE_EVENT_MAX 4096

static inline int ff_memepipe_event_validate_fd(int fd)
{
    struct stat st;

    if (fd < 3)
        return AVERROR(EINVAL);
    if (fstat(fd, &st) < 0)
        return AVERROR(errno);
    if (!S_ISFIFO(st.st_mode))
        return AVERROR(EINVAL);
    return 0;
}

/*
 * Emit one complete record with one write(2).  Records are kept below
 * PIPE_BUF on every supported host, so simultaneous playout and WHEP writes
 * cannot interleave.  A configured channel is authoritative: failure is
 * returned to the media component so the exact process generation exits
 * instead of continuing in an unknowable control state.
 */
static inline int ff_memepipe_event_emit(int fd, const char *kind,
                                         const char *format, ...)
{
    char line[MEMEPIPE_EVENT_MAX];
    size_t used;
    int ret;
    va_list ap;

    if (fd < 0)
        return 0;
    ret = snprintf(line, sizeof(line), MEMEPIPE_EVENT_PREFIX "%s", kind);
    if (ret < 0 || ret >= (int)sizeof(line) - 1)
        return AVERROR(ENOSPC);
    used = ret;
    if (format && *format) {
        line[used++] = '\t';
        va_start(ap, format);
        ret = vsnprintf(line + used, sizeof(line) - used, format, ap);
        va_end(ap);
        if (ret < 0 || ret >= (int)(sizeof(line) - used - 1))
            return AVERROR(ENOSPC);
        used += ret;
    }
    line[used++] = '\n';

    do {
        ret = write(fd, line, used);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0)
        return AVERROR(errno);
    if ((size_t)ret != used)
        return AVERROR(EIO);
    return 0;
}

#endif /* AVFORMAT_MEMEPIPE_EVENT_H */
