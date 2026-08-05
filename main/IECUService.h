#include <cstdint>


#ifndef IECUSERVICE_H
#define IECUSERVICE_H

enum ECUServiceErrorCode
{
    ECU_SERVICE_ERROR_NONE = 0,
    ECU_SERVICE_ERROR_FUNCTION_UNDEFINED = 1,
    ECU_SERVICE_ERROR_TIMEOUT = 2,
    ECU_SERVICE_ERROR_BAD_RESPONSE = 3,
    ECU_SERVICE_ERROR_FUNCTION_NOT_IMPLEMENTED = 4,
    ECU_SERVICE_ERROR_BAD_KEY = 5
};

struct ECUInformation
{
    uint8_t ECUVin[17];
};

class IECUService 
{
    protected:

    public:
        virtual ~IECUService() = default;
        virtual ECUServiceErrorCode Connect();
        virtual ECUServiceErrorCode Disconnect();
        virtual ECUServiceErrorCode SuppressBroadcastMessages(bool suppress);
        virtual ECUServiceErrorCode QueryInformation(ECUInformation *info);
        virtual ECUServiceErrorCode IncreaseCommunicationSpeed();
        virtual ECUServiceErrorCode DecreaseCommunicationSpeed();
        virtual ECUServiceErrorCode UnlockECU();
        virtual ECUServiceErrorCode LoadKernel(uint8_t *kernelData = 0, uint32_t kernelSize = 0, uint32_t kernelLoadAddress = 0, uint32_t kernelRunAddress = 0);
        virtual ECUServiceErrorCode ReadMemory(uint32_t address, uint8_t *buffer, uint32_t length);
        virtual ECUServiceErrorCode WriteMemory(uint32_t address, const uint8_t *buffer, uint32_t length);
        virtual ECUServiceErrorCode ResetECU();
};

#endif