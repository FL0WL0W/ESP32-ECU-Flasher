#include "ECUService_E78.h"
#include <cstring>
#include <algorithm>
#include <esp_log.h>

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
        return ECU_SERVICE_ERROR_FUNCTION_NOT_IMPLEMENTED; 
    }
    else
    {
        // Invalid seed length
        return ECU_SERVICE_ERROR_FUNCTION_NOT_IMPLEMENTED; 
    }
    return ECU_SERVICE_ERROR_NONE;
}