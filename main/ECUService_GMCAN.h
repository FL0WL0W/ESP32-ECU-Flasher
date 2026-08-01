#include "IECUService.h"
#include "ICommunicationService.h"

#ifndef IECUSERVICE_GMCAN_H
#define IECUSERVICE_GMCAN_H

class ECUService_GMCAN : public IECUService
{
    protected:
        EmbeddedIOServices::ICommunicationService *_ecuISOTPService;
        std::function<void(uint32_t)> _delay;
        uint8_t *_kernelData;
        uint32_t _kernelSize;
        uint32_t _kernelLoadAddress;
        uint32_t _kernelRunAddress;
        virtual ECUServiceErrorCode SeedKeyAlgorithm(uint8_t *key, const uint8_t *seed, uint8_t seedLength);
        void WaitForReadyAndSend(uint8_t *data, size_t length, uint32_t timeoutMs = 500);
        ECUServiceErrorCode SendAndWaitForResponse(uint8_t *data, size_t length, std::function<bool(const void *data, size_t length, ECUServiceErrorCode *errorCode)> responseCondition, uint32_t timeoutMs = 500);
    public:
        ECUService_GMCAN(EmbeddedIOServices::ICommunicationService *ecuISOTPService, std::function<void(uint32_t)> delay);
        ~ECUService_GMCAN() override;
        ECUServiceErrorCode Connect() override;
        ECUServiceErrorCode Disconnect() override;
        ECUServiceErrorCode SuppressBroadcastMessages(bool suppress) override;
        ECUServiceErrorCode QueryInformation(ECUInformation *info) override;
        ECUServiceErrorCode IncreaseCommunicationSpeed() override;
        ECUServiceErrorCode DecreaseCommunicationSpeed() override;
        ECUServiceErrorCode UnlockECU() override;
        ECUServiceErrorCode LoadKernel() override;
        ECUServiceErrorCode ReadMemory(uint32_t address, uint8_t *buffer, uint32_t length) override;
        ECUServiceErrorCode EraseMemory(uint32_t address, uint32_t length) override;
        ECUServiceErrorCode WriteMemory(uint32_t address, const uint8_t *buffer, uint32_t length) override;
        ECUServiceErrorCode VerifyMemory(uint32_t address, uint32_t length, uint32_t crc) override;
        ECUServiceErrorCode ResetECU() override;
};

#endif