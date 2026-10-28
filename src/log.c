#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static FILE *g_fp;
static log_level_t g_level = LOG_LEVEL_INFO;

static const char *const g_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

const char *log_level_name(log_level_t level)
{
    if (level < LOG_LEVEL_DEBUG || level > LOG_LEVEL_ERROR)
        return "?";
    return g_names[level];
}

int log_level_parse(const char *s, log_level_t *out)
{
    if (!s || !out)
        return -1;

    if (!strcasecmp(s, "DEBUG"))
        *out = LOG_LEVEL_DEBUG;
    else if (!strcasecmp(s, "INFO"))
        *out = LOG_LEVEL_INFO;
    else if (!strcasecmp(s, "WARN"))
        *out = LOG_LEVEL_WARN;
    else if (!strcasecmp(s, "ERROR"))
        *out = LOG_LEVEL_ERROR;
    else
        return -1;

    return 0;
}

int log_init(log_level_t level, const char *path)
{
    g_level = level;

    if (g_fp) {
        fclose(g_fp);
        g_fp = NULL;
    }

    if (path && *path) {
        g_fp = fopen(path, "a");
        if (!g_fp)
            return -1;
        setvbuf(g_fp, NULL, _IOLBF, 0);
    }

    return 0;
}

void log_close(void)
{
    if (g_fp) {
        fclose(g_fp);
        g_fp = NULL;
    }
}

void log_write(log_level_t level, const char *file, int line,
               const char *fmt, ...)
{
    if (level < g_level)
        return;

    int saved_errno = errno;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm);

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;

    flockfile(stderr);
    fprintf(stderr, "[%s.%03ld] [%s] %s:%d: %s\n",
            tbuf, ts.tv_nsec / 1000000L,
            log_level_name(level), base, line, msg);

    if (g_fp) {
        fprintf(g_fp, "[%s.%03ld] [%s] %s:%d: %s\n",
                tbuf, ts.tv_nsec / 1000000L,
                log_level_name(level), base, line, msg);
    }
    funlockfile(stderr);

    if (level >= LOG_LEVEL_WARN) {
        fflush(stderr);
        if (g_fp)
            fflush(g_fp);
    }

    errno = saved_errno;
}

