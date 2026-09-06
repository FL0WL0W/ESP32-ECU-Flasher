#include "ECUService_E78.h"
#include <cstring>
#include <algorithm>
#include <esp_log.h>
#include "mbedtls/sha256.h"
#include <mbedtls/aes.h>

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
        // Not implemented yet
        const uint8_t iterations = 10 - seed[4];
        uint8_t secret[32] = {
            0xDB, 0x5C, 0xB0, 0x76, 0x1E, 0xE9, 0xD1, 0x82,
            0xBA, 0x31, 0x2F, 0x8E, 0xF5, 0x58, 0x3C, 0x89,
            0xA1, 0x4C, 0x01, 0xD9, 0xBA, 0x7D, 0xA1, 0x81,
            0x7B, 0xFA, 0x0F, 0x21, 0xE2, 0x7A, 0xB3, 0x6E
        }; 
        for (int i = 0; i < iterations; i++)
            mbedtls_sha256(secret, 32, secret, 0);
        
        uint8_t block[16];
        memset(block, 0xFF, 11);
        memcpy(block + 11, seed, 5);
        
        // AES-128 ECB encrypt
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, secret, 128);
        
        uint8_t ciphertext[16];
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, block, ciphertext);
        mbedtls_aes_free(&aes);

        key[0] = ciphertext[0];
        key[1] = ciphertext[1];
        key[2] = ciphertext[2];
        key[3] = ciphertext[3];
        key[4] = ciphertext[4];
    }
    else
    {
        // Invalid seed length
        return ECU_SERVICE_ERROR_FUNCTION_NOT_IMPLEMENTED; 
    }
    return ECU_SERVICE_ERROR_NONE;
}