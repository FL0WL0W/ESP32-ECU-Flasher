#include "ECUService_E38.h"
#include <cstring>
#include <algorithm>
#include <esp_log.h>

using namespace EmbeddedIOServices;

extern const uint8_t kernel_e38_start[] asm("_binary_Kernel_E38_bin_start");
extern const uint8_t kernel_e38_end[]   asm("_binary_Kernel_E38_bin_end");

ECUService_E38::ECUService_E38(ICommunicationService *ecuISOTPService, std::function<void(uint32_t)> delay)
    : ECUService_GMCAN(ecuISOTPService, delay)
{
    _kernelSize = kernel_e38_end - kernel_e38_start;
    _kernelData = new uint8_t[_kernelSize];
    std::memcpy(_kernelData, kernel_e38_start, _kernelSize);
    _kernelLoadAddress = 0x003FC430;
    _kernelRunAddress = 0x003FC434;
}

ECUService_E38::~ECUService_E38()
{
}

ECUServiceErrorCode ECUService_E38::SeedKeyAlgorithm(uint8_t *key, const uint8_t *seed, uint8_t seedLength)
{
    if(seedLength == 2)
    {
        uint16_t u16key = seed[0] | (seed[1] << 8); //swap
        u16key = (u16key + 0x7D58) & 0xFFFF; //add magic
        u16key = (~u16key) & 0xFFFF;  // bitwise not
        u16key = (u16key + 0x8001) & 0xFFFF; //add magic
        //swap
        key[1] = static_cast<uint8_t>(u16key >> 8);
        key[0] = static_cast<uint8_t>(u16key & 0xFF);
    }
    else
    {
        // Invalid seed length
        return ECU_SERVICE_ERROR_FUNCTION_NOT_IMPLEMENTED; 
    }
    return ECU_SERVICE_ERROR_NONE;
}