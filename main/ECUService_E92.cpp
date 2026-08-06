#include "ECUService_E92.h"
#include <cstring>
#include <algorithm>
#include <esp_log.h>

using namespace EmbeddedIOServices;

ECUService_E92::ECUService_E92(ICommunicationService *ecuISOTPService, std::function<void(uint32_t)> delay)
    : ECUService_GMCAN(ecuISOTPService, delay)
{
}

ECUService_E92::~ECUService_E92()
{
}

ECUServiceErrorCode ECUService_E92::SeedKeyAlgorithm(uint8_t *key, const uint8_t *seed, uint8_t seedLength)
{
    if(seedLength == 2)
    {
        uint16_t u16key = seed[0] | seed[1] << 8; //swap
        u16key = (u16key + 0xA442) & 0xFFFF; //add magic
        u16key = ((u16key >> 4) | (u16key << 12)) & 0xFFFF; //ror4
        u16key = (u16key + 0xFF37) & 0xFFFF; //add magic
        u16key = (~u16key) & 0xFFFF;  // bitwise not
        key[0] = static_cast<uint8_t>(u16key >> 8);
        key[1] = static_cast<uint8_t>(u16key & 0xFF);
    }
    else if(seedLength == 5)
    {
        // Not implemented yet
        return ECU_SERVICE_ERROR_FUNCTION_NOT_IMPLEMENTED; 
    }
    else
    {
        // Invalid seed length
        return ECU_SERVICE_ERROR_FUNCTION_NOT_IMPLEMENTED; 
    }
    return ECU_SERVICE_ERROR_NONE;
}