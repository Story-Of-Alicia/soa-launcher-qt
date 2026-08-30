#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*courier_log_cb)(int level, const char* message, void* ctx);

void courier_set_log_callback(courier_log_cb callback, void* ctx);

typedef struct courier courier;

typedef enum
{
    courier_phase_preparing   = 0,
    courier_phase_checking    = 1,
    courier_phase_downloading = 2,
    courier_phase_verifying   = 3
} courier_phase;

typedef void (*courier_progress_cb)(uint64_t    operation_id,
                                    courier_phase phase,
                                    const char*   message,
                                    int           percent,
                                    uint64_t      received,
                                    uint64_t      total,
                                    uint64_t      throughput,
                                    int           file_index,
                                    int           file_count,
                                    void*         ctx);

typedef enum
{
    courier_result_completed        = 0,
    courier_result_up_to_date       = 1,
    courier_result_update_available = 2,
    courier_result_cancelled        = 3,
    courier_result_failed           = 4
} courier_result;

typedef void (*courier_done_cb)(uint64_t       operation_id,
                                courier_result result,
                                const char*    message,
                                void*          ctx);

courier* courier_create(const char* cdn_base_url,
                        courier_progress_cb on_progress,
                        courier_done_cb on_done,
                        void* ctx);
void courier_destroy(courier* d);

uint64_t courier_integrity_check(courier* d, const char* install_path);
uint64_t courier_update_check(courier* d, const char* install_path);
uint64_t courier_update(courier* d, const char* install_path);

void courier_cancel(courier* d);

#ifdef __cplusplus
}
#endif
