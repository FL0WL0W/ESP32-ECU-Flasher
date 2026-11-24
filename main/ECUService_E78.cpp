#include "ECUService_E78.h"
#include <cstring>
#include <algorithm>
#include <esp_log.h>
#include "psa/crypto.h"

using namespace EmbeddedIOServices;

ECUService_E78::ECUService_E78(ICommunicationService *ecuISOTPService, std::function<void(uint32_t)> delay)
    : ECUService_GMCAN(ecuISOTPService, delay)
{
}

ECUService_E78::~ECUService_E78()
{
}

ECUServiceErrorCode ECUService_E78::SeedKeyAlgorithm(uint8_t *key, const uint8_t *seed, uint8_t seedLength)
{
    if(seedLength == 2)
    {
        uint16_t u16key = seed[0] | seed[1] << 8; //swap
        u16key = u16key + 0x0801; //add magic
        u16key = (u16key >> 8) | (u16key << 8); //swap
        u16key = u16key + 0x5063; //add magic
        u16key = (u16key >> 1) | (u16key << 15); //ror1
        key[0] = static_cast<uint8_t>(u16key >> 8);
        key[1] = static_cast<uint8_t>(u16key & 0xFF);
    }
    else if(seedLength == 5)
    {
        // Ensure PSA Crypto is initialized once at startup (safe to call multiple times)
        psa_crypto_init();
        
        const uint8_t iterations = 10 - seed[4];
        uint8_t secret[32] = {
            0xDB, 0x5C, 0xB0, 0x76, 0x1E, 0xE9, 0xD1, 0x82,
            0xBA, 0x31, 0x2F, 0x8E, 0xF5, 0x58, 0x3C, 0x89,
            0xA1, 0x4C, 0x01, 0xD9, 0xBA, 0x7D, 0xA1, 0x81,
            0x7B, 0xFA, 0x0F, 0x21, 0xE2, 0x7A, 0xB3, 0x6E
        }; 
        
        uint8_t tmp[32];
        size_t hash_len = 0;

        for (int i = 0; i < iterations; i++) {
            psa_status_t status = psa_hash_compute(
                PSA_ALG_SHA_256,
                secret, 32,
                tmp, sizeof(tmp),
                &hash_len
            );
            if (status != PSA_SUCCESS) {
                // Handle error
                break;
            }
            memcpy(secret, tmp, 32);
        }
        
        uint8_t block[16];
        memset(block, 0xFF, 11);
        memcpy(block + 11, seed, 5);

        // 1. Import the 16-byte (128-bit) AES key
        psa_key_id_t key_id = 0;
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
        psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
        psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
        psa_set_key_bits(&attributes, 128);

        psa_status_t status = psa_import_key(&attributes, secret, 16, &key_id);
        psa_reset_key_attributes(&attributes);

        // 2. Encrypt the 16-byte block
        uint8_t ciphertext[16];
        size_t output_len = 0;

        if (status == PSA_SUCCESS) {
            status = psa_cipher_encrypt(
                key_id,
                PSA_ALG_ECB_NO_PADDING,
                block, 16,
                ciphertext, sizeof(ciphertext),
                &output_len
            );
        }

        // 3. Destroy the key handle to free resources (replaces mbedtls_aes_free)
        psa_destroy_key(key_id);

        memcpy(key, ciphertext, 5);
    }
    else
    {
        // Invalid seed length
        return ECU_SERVICE_ERROR_FUNCTION_NOT_IMPLEMENTED; 
    }
    return ECU_SERVICE_ERROR_NONE;
}