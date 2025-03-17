#include <stdlib.h>
#include <assert.h>
#include "znprofiler.h"

#include <znutil.h>

struct zn_profiler *
zn_profiler_init(const char *filename) {
    assert(filename);

    struct zn_profiler *zp = malloc(sizeof(struct zn_profiler));
    if (zp == NULL) {
        return NULL;
    }

    zp->fp = fopen(filename, "w");
    if (zp->fp == NULL) {
        dbg_printf("Failed to open file %s\n", filename);
        free(zp);
        return NULL;
    }

    // Enable buffering
    setvbuf(zp->fp, zp->buffer, _IOFBF, METRICS_BUFFER_SIZE);

    zn_profiler_write(zp, "%s\n", PROFILING_HEADERS);

    return zp;
}

void
zn_profiler_close(struct zn_profiler *zp) {
    fflush(zp->fp);
    free(zp);
}

void
zn_profiler_write(struct zn_profiler *zp, const char *format, ...) {
    va_list args;
    va_start(args, format);

    vfprintf(zp->fp, format, args);

    va_end(args);
}