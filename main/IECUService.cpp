#include "IECUService.h"

ECUServiceErrorCode IECUService::Connect()
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::Disconnect()
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::SuppressBroadcastMessages(bool suppress)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::QueryInformation(ECUInformation *info)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::IncreaseCommunicationSpeed()
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::DecreaseCommunicationSpeed()
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::UnlockECU()
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::LoadKernel()
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::ReadMemory(uint32_t address, uint8_t *buffer, uint32_t length)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::EraseMemory(uint32_t address, uint32_t length)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::WriteMemory(uint32_t address, const uint8_t *buffer, uint32_t length)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::VerifyMemory(uint32_t address, uint32_t length, uint32_t crc)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode IECUService::ResetECU()
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}