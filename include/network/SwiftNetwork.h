#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct soa_http_client soa_http_client;

typedef enum
{
    soa_http_result_completed = 0,
    soa_http_result_cancelled = 1,
    soa_http_result_failed = 2
} soa_http_result;

typedef enum
{
    soa_http_method_get = 0,
    soa_http_method_head = 1
} soa_http_method;

typedef void (*soa_http_done_cb)(uint64_t request_id,
                                 soa_http_result result,
                                 int http_status,
                                 const uint8_t* data,
                                 uint64_t data_size,
                                 const char* final_url,
                                 const char* error,
                                 uint64_t elapsed_ms,
                                 void* ctx);

typedef void (*soa_dns_done_cb)(uint64_t request_id,
                                soa_http_result result,
                                const char* address,
                                const char* error,
                                uint64_t elapsed_ms,
                                void* ctx);

soa_http_client* soa_http_client_create(soa_http_done_cb http_done,
                                        soa_dns_done_cb dns_done,
                                        void* ctx);
void soa_http_client_shutdown(soa_http_client* client);
void soa_http_client_destroy(soa_http_client* client);
uint64_t soa_http_client_request(soa_http_client* client,
                                 const char* url,
                                 soa_http_method method,
                                 uint32_t timeout_ms,
                                 uint64_t maximum_bytes,
                                 const char* accept,
                                 const char* user_agent,
                                 bool allow_insecure_http);
uint64_t soa_http_client_resolve(soa_http_client* client,
                                 const char* hostname,
                                 uint32_t timeout_ms);
void soa_http_client_cancel(soa_http_client* client, uint64_t request_id);
void soa_http_client_cancel_all(soa_http_client* client);

typedef struct soa_launcher_updater soa_launcher_updater;

typedef enum
{
    soa_launcher_check_no_update = 0,
    soa_launcher_check_update_available = 1,
    soa_launcher_check_cancelled = 2,
    soa_launcher_check_failed = 3
} soa_launcher_check_result;

typedef enum
{
    soa_launcher_download_completed = 0,
    soa_launcher_download_cancelled = 1,
    soa_launcher_download_failed = 2
} soa_launcher_download_result;

typedef enum
{
    soa_launcher_error_none = 0,
    soa_launcher_error_busy = 1,
    soa_launcher_error_invalid_configuration = 2,
    soa_launcher_error_network = 3,
    soa_launcher_error_http = 4,
    soa_launcher_error_response_too_large = 5,
    soa_launcher_error_invalid_release = 6,
    soa_launcher_error_missing_asset = 7,
    soa_launcher_error_unsafe_url = 8,
    soa_launcher_error_missing_digest = 9,
    soa_launcher_error_invalid_size = 10,
    soa_launcher_error_destination = 11,
    soa_launcher_error_write = 12,
    soa_launcher_error_size_mismatch = 13,
    soa_launcher_error_digest_mismatch = 14,
    soa_launcher_error_finalize = 15,
    soa_launcher_error_cancelled = 16,
    soa_launcher_error_invalid_signature = 17
} soa_launcher_error;

typedef void (*soa_launcher_check_cb)(soa_launcher_check_result result,
                                      soa_launcher_error error_code,
                                      int http_status,
                                      const char* error_detail,
                                      const char* version,
                                      const char* minimum_version,
                                      const char* message,
                                      const char* package_kind,
                                      const char* package_file_name,
                                      const char* package_url,
                                      const char* sha256,
                                      uint64_t expected_size,
                                      bool required,
                                      const char* releases_json,
                                      void* ctx);

typedef void (*soa_launcher_progress_cb)(uint64_t received,
                                         uint64_t total,
                                         void* ctx);

typedef void (*soa_launcher_download_cb)(soa_launcher_download_result result,
                                         soa_launcher_error error_code,
                                         int http_status,
                                         const char* error_detail,
                                         const char* final_path,
                                         void* ctx);

soa_launcher_updater* soa_launcher_updater_create(const char* manifest_url,
                                                  const char* fallback_manifest_url,
                                                  const char* public_key_hex,
                                                  const char* current_version,
                                                  const char* platform,
                                                  const char* download_directory,
                                                  const char* user_agent,
                                                  bool allow_insecure_http,
                                                  soa_launcher_check_cb check_done,
                                                  soa_launcher_progress_cb progress,
                                                  soa_launcher_download_cb download_done,
                                                  void* ctx);
void soa_launcher_updater_shutdown(soa_launcher_updater* updater);
void soa_launcher_updater_destroy(soa_launcher_updater* updater);
void soa_launcher_updater_check(soa_launcher_updater* updater);
void soa_launcher_updater_download(soa_launcher_updater* updater);
void soa_launcher_updater_cancel(soa_launcher_updater* updater);
bool soa_launcher_updater_select_version(soa_launcher_updater* updater, const char* version);

bool soa_verify_soa_seal_v1(const uint8_t* public_key,
                            uint64_t public_key_size,
                            uint8_t document_kind,
                            const uint8_t* message,
                            uint64_t message_size,
                            const char* key_id,
                            uint64_t key_id_size,
                            const uint8_t* signature,
                            uint64_t signature_size);

typedef struct soa_discord_rpc soa_discord_rpc;

soa_discord_rpc* soa_discord_rpc_create(const char* application_id,
                                        int64_t process_id,
                                        const char* proxy_base_url);
void soa_discord_rpc_set_launcher_presence(soa_discord_rpc* rpc);
void soa_discord_rpc_set_game_presence(soa_discord_rpc* rpc, const char* username);
void soa_discord_rpc_shutdown(soa_discord_rpc* rpc);
void soa_discord_rpc_destroy(soa_discord_rpc* rpc);

#ifdef __cplusplus
}
#endif
