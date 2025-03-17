#ifndef PROFILING_H
#define PROFILING_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <glib.h>

#define METRICS_BUFFER_SIZE (1u << 17)  // 2^17 = 131072
#define PROFILING_INTERVAL_SEC 2
#define PROFILING_HEADERS "METRIC,VALUES..."


struct zn_profiler_metrics {
    uint32_t count;
    double value;
};

#define PROFILING_METRICS 1 // Keep in sync with enum and zn_profiler_metric_names
enum zn_profiler_tag {
    ZN_PROFILER_METRIC_GET_LATENCY = 0
};
extern char *zn_profiler_metric_names[PROFILING_METRICS]; // (in znprofiler.c)

struct zn_profiler {
    FILE *fp;
    char buffer[METRICS_BUFFER_SIZE];
    struct zn_profiler_metrics metrics[PROFILING_METRICS];
    bool realtime;
    GMutex lock;
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
 *
* LOCKED BY CALLEE
 */
void
zn_profiler_write(struct zn_profiler *zp, const char *format, ...);

/**
 * Write all metrics out and reset counters
 *
* LOCKED BY CALLEE
 */
void
zn_profiler_write_all_and_reset(struct zn_profiler *zp);

/**
 * @brief Increments the specified metric's total value and usage count.
 *
 * This function adds the provided @p value to the selected metric's
 * cumulative total in the profiler and increments the metric's usage count
 * by one.
 *
 * LOCKED BY CALLEE
 *
 * @param[in,out] zp      Pointer to the profiler structure managing the metrics.
 * @param[in]     metric  Enum identifier of the metric to update.
 * @param[in]     value   The amount to add to the metric's current total.
 */
void
zn_profiler_update_metric(struct zn_profiler *zp, enum zn_profiler_tag metric, uint32_t value);

/**
* Calls zn_profiler_update_metric if zp not NULL
*/
#define ZN_PROFILER_UPDATE(zp, metric, value)    \
    do {                                    \
        if ((zp) != NULL) {                \
            zn_profiler_update_metric((zp), (metric), (value));     \
        }                                   \
    } while (0)

/**
 * @brief Resets the specified metric's value and usage count to zero.
 *
 * This function sets both the cumulative value and usage count for the
 * given metric to zero, clearing any collected data.
 *
 * LOCKED BY **CALLER**
 *
 * @param[in,out] zp      Pointer to the profiler structure managing the metrics.
 * @param[in]     metric  Enum identifier of the metric to reset.
 */
void
zn_profiler_reset_metric(struct zn_profiler *zp, enum zn_profiler_tag metric);


#endif //PROFILING_H
