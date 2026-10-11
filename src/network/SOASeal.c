#include "network/SwiftNetwork.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#define SOA_SEAL_KEY_ID_SIZE 44u
#define SOA_SEAL_SIGNATURE_SIZE 64u

static const uint8_t soa_seal_key_id_domain[] = "SOA-SEAL-KEY-ID-V1\0";
static const uint8_t soa_seal_prefix[] = "SOA-SEAL-V1\0";
static const uint8_t soa_seal_manifest_kind[] = "kind=manifest\0";
static const uint8_t soa_seal_history_kind[] = "kind=history\0";
static const uint8_t soa_seal_key_prefix[] = "key=";

static bool soa_seal_key_id(const uint8_t* public_key,
                            const size_t public_key_size,
                            char output[SOA_SEAL_KEY_ID_SIZE + 1])
{
    if (public_key == NULL || public_key_size != 32)
        return false;

    EVP_MD_CTX* digest = EVP_MD_CTX_new();
    if (digest == NULL)
        return false;

    uint8_t hash[32];
    unsigned int hash_size = 0;
    const int ok = EVP_DigestInit_ex(digest, EVP_sha256(), NULL) == 1
        && EVP_DigestUpdate(digest,
                            soa_seal_key_id_domain,
                            sizeof(soa_seal_key_id_domain) - 1) == 1
        && EVP_DigestUpdate(digest, public_key, public_key_size) == 1
        && EVP_DigestFinal_ex(digest, hash, &hash_size) == 1;
    EVP_MD_CTX_free(digest);

    if (!ok || hash_size != sizeof(hash))
        return false;

    static const char hex[] = "0123456789ABCDEF";
    size_t position = 0;
    memcpy(output + position, "SOA1-", 5);
    position += 5;

    for (size_t index = 0; index < 16; ++index)
    {
        if (index != 0 && (index % 2) == 0)
            output[position++] = '-';
        output[position++] = hex[(hash[index] >> 4) & 0x0f];
        output[position++] = hex[hash[index] & 0x0f];
    }

    output[position] = '\0';
    return position == SOA_SEAL_KEY_ID_SIZE;
}

bool soa_verify_soa_seal_v1(const uint8_t* public_key,
                            const uint64_t public_key_size,
                            const uint8_t document_kind,
                            const uint8_t* message,
                            const uint64_t message_size,
                            const char* key_id,
                            const uint64_t key_id_size,
                            const uint8_t* signature,
                            const uint64_t signature_size)
{
    if (public_key == NULL || public_key_size != 32 ||
        key_id == NULL || key_id_size != SOA_SEAL_KEY_ID_SIZE ||
        signature == NULL || signature_size != SOA_SEAL_SIGNATURE_SIZE ||
        (message == NULL && message_size != 0) || message_size > SIZE_MAX)
    {
        return false;
    }

    const uint8_t* kind = NULL;
    size_t kind_size = 0;
    if (document_kind == 1)
    {
        kind = soa_seal_manifest_kind;
        kind_size = sizeof(soa_seal_manifest_kind) - 1;
    }
    else if (document_kind == 2)
    {
        kind = soa_seal_history_kind;
        kind_size = sizeof(soa_seal_history_kind) - 1;
    }
    else
    {
        return false;
    }

    char expected_key_id[SOA_SEAL_KEY_ID_SIZE + 1];
    if (!soa_seal_key_id(public_key, (size_t)public_key_size, expected_key_id) ||
        memcmp(expected_key_id, key_id, SOA_SEAL_KEY_ID_SIZE) != 0)
    {
        return false;
    }

    const size_t prefix_size = sizeof(soa_seal_prefix) - 1;
    const size_t key_prefix_size = sizeof(soa_seal_key_prefix) - 1;
    const size_t fixed_size = prefix_size + kind_size + key_prefix_size +
                              SOA_SEAL_KEY_ID_SIZE + 1;
    if ((size_t)message_size > SIZE_MAX - fixed_size)
        return false;

    const size_t payload_size = fixed_size + (size_t)message_size;
    uint8_t* payload = malloc(payload_size == 0 ? 1 : payload_size);
    if (payload == NULL)
        return false;

    size_t offset = 0;
    memcpy(payload + offset, soa_seal_prefix, prefix_size);
    offset += prefix_size;
    memcpy(payload + offset, kind, kind_size);
    offset += kind_size;
    memcpy(payload + offset, soa_seal_key_prefix, key_prefix_size);
    offset += key_prefix_size;
    memcpy(payload + offset, key_id, SOA_SEAL_KEY_ID_SIZE);
    offset += SOA_SEAL_KEY_ID_SIZE;
    payload[offset++] = '\0';
    if (message_size != 0)
        memcpy(payload + offset, message, (size_t)message_size);

    EVP_PKEY* key = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, NULL, public_key, (size_t)public_key_size);
    if (key == NULL)
    {
        free(payload);
        return false;
    }

    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == NULL)
    {
        EVP_PKEY_free(key);
        free(payload);
        return false;
    }

    const int initialized = EVP_DigestVerifyInit(context, NULL, NULL, NULL, key);
    const int verified = initialized == 1
        ? EVP_DigestVerify(context,
                           signature,
                           (size_t)signature_size,
                           payload,
                           payload_size)
        : 0;

    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    free(payload);
    return verified == 1;
}
