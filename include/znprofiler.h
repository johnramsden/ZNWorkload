#ifndef PROFILING_H
#define PROFILING_H

#include <stdio.h>

#define METRICS_BUFFER_SIZE (1u << 17)  // 2^17 = 131072
#define PROFILING_INTERVAL_SEC 2
#define PROFILING_HEADERS "METRIC,VALUES..."

struct zn_profiler {
    FILE *fp;
    char buffer[METRICS_BUFFER_SIZE];
};

/**
 * Initialize the profiler
 *
 * @param filename File to output metrics to
 * @return Profiler or NULL on error
 */
struct zn_profiler *
zn_profiler_init(const char *filename);

/**
 * Close and flush profiler
 * @param zp Profiler
 */
void
zn_profiler_close(struct zn_profiler *zp);

/**
 * Write metrics to profiler
 */
void
zn_profiler_write(struct zn_profiler *zp, const char *format, ...);

#endif //PROFILING_H
