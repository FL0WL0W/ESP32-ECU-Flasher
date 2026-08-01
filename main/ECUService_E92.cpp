#include "ECUService_E92.h"
#include <cstring>
#include <algorithm>
#include <esp_log.h>

using namespace EmbeddedIOServices;

extern const uint8_t kernel_e92_start[] asm("_binary_Kernel_E92_bin_start");
extern const uint8_t kernel_e92_end[]   asm("_binary_Kernel_E92_bin_end");

ECUService_E92::ECUService_E92(ICommunicationService *ecuISOTPService, std::function<void(uint32_t)> delay)
    : ECUService_GMCAN(ecuISOTPService, delay)
{
    _kernelSize = kernel_e92_end - kernel_e92_start;
    _kernelData = new uint8_t[_kernelSize];
    std::memcpy(_kernelData, kernel_e92_start, _kernelSize);
    _kernelLoadAddress = 0x40020000;
    _kernelRunAddress = 0x40020000;
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