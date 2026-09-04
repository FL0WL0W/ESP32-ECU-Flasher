#ifndef UDS_H
#define UDS_H

#include "ICommunicationService.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <vector>

enum class UDSStatus
{
    Ok,
    Timeout,
    NegativeResponse,
    InvalidArgument,
    CompressionError
};

struct UDSResponse
{
    UDSStatus status = UDSStatus::Ok;
    uint8_t negativeResponseCode = 0;
    std::vector<uint8_t> payload;

    bool Ok() const { return status == UDSStatus::Ok; }
};

struct UDSFlashBlock
{
    uint8_t id = 0;
    uint32_t address = 0;
    uint32_t length = 0;
    std::array<uint8_t, 32> sha256{};
};

class UDS
{
    public:
        UDS(EmbeddedIOServices::ICommunicationService *transport,
            std::function<void(uint32_t)> delay,
            uint32_t responseTimeoutMs = 500,
            uint32_t responsePendingTimeoutMs = 5000);

        UDSResponse Request(uint8_t service,
                            const uint8_t *payload = nullptr,
                            size_t payloadLength = 0,
                            bool expectResponse = true);
        UDSResponse Request(uint8_t service,
                            const std::vector<uint8_t> &payload,
                            bool expectResponse = true);

        UDSResponse DiagnosticSessionControl(uint8_t session, bool suppressPositiveResponse = false);
        UDSResponse ECUReset(uint8_t resetType, bool suppressPositiveResponse = false);
        UDSResponse ClearDiagnosticInformation(uint32_t dtcGroup = 0xFFFFFF);
        UDSResponse ReadDTCInformation(uint8_t subFunction, const std::vector<uint8_t> &parameters = {});
        UDSResponse ReadDataByIdentifier(const std::vector<uint16_t> &identifiers);
        UDSResponse ReadMemoryByAddress(uint64_t address, uint64_t size, uint8_t addressLength = 4, uint8_t sizeLength = 4);
        UDSResponse ReadScalingDataByIdentifier(uint16_t identifier);
        UDSResponse SecurityAccessRequestSeed(uint8_t level, const std::vector<uint8_t> &parameters = {});
        UDSResponse SecurityAccessSendKey(uint8_t level, const std::vector<uint8_t> &key);
        UDSResponse CommunicationControl(uint8_t controlType, uint8_t communicationType, const std::vector<uint8_t> &nodeIdentification = {}, bool suppressPositiveResponse = false);
        UDSResponse Authentication(uint8_t subFunction, const std::vector<uint8_t> &parameters = {}, bool suppressPositiveResponse = false);
        UDSResponse ReadDataByPeriodicIdentifier(uint8_t transmissionMode, const std::vector<uint8_t> &identifiers);
        UDSResponse DynamicallyDefineDataIdentifier(uint8_t subFunction, const std::vector<uint8_t> &definition);
        UDSResponse TesterPresent(bool suppressPositiveResponse = false);
        UDSResponse AccessTimingParameter(uint8_t subFunction, const std::vector<uint8_t> &parameters = {});
        UDSResponse SecuredDataTransmission(const std::vector<uint8_t> &data);
        UDSResponse ControlDTCSetting(uint8_t settingType, const std::vector<uint8_t> &optionRecord = {}, bool suppressPositiveResponse = false);
        UDSResponse ResponseOnEvent(uint8_t eventType, const std::vector<uint8_t> &eventWindow = {}, bool suppressPositiveResponse = false);
        UDSResponse LinkControl(uint8_t controlType, const std::vector<uint8_t> &parameters = {}, bool suppressPositiveResponse = false);
        UDSResponse WriteDataByIdentifier(uint16_t identifier, const std::vector<uint8_t> &data);
        UDSResponse InputOutputControlByIdentifier(uint16_t identifier, uint8_t controlParameter, const std::vector<uint8_t> &controlState = {});
        UDSResponse RoutineControl(uint8_t routineType, uint16_t routineIdentifier, const std::vector<uint8_t> &optionRecord = {});
        UDSResponse GetFlashBlockMap(std::vector<UDSFlashBlock> &blocks);
        UDSResponse QueueEraseFlashBlock(uint8_t blockId);
        UDSResponse RequestDownload(uint64_t address, uint64_t size, uint8_t dataFormatIdentifier = 0x10, uint8_t addressLength = 4, uint8_t sizeLength = 4);
        UDSResponse RequestUpload(uint64_t address, uint64_t size, uint8_t dataFormatIdentifier = 0x10, uint8_t addressLength = 4, uint8_t sizeLength = 4);
        UDSResponse TransferData(uint8_t blockSequenceCounter, const std::vector<uint8_t> &data = {});
        UDSResponse RequestTransferExit(const std::vector<uint8_t> &parameters = {});
        UDSResponse RequestFileTransfer(uint8_t modeOfOperation, const std::vector<uint8_t> &parameters);
        UDSResponse WriteMemoryByAddress(uint64_t address, const std::vector<uint8_t> &data, uint8_t addressLength = 4, uint8_t sizeLength = 4);
        bool UsesLZ4Compression() const { return _lz4Transfer; }
        size_t MaximumBlockLength() const { return _maximumBlockLength; }

    private:
        EmbeddedIOServices::ICommunicationService *_transport;
        std::function<void(uint32_t)> _delay;
        uint32_t _responseTimeoutMs;
        uint32_t _responsePendingTimeoutMs;
        enum class TransferDirection { None, Download, Upload };
        TransferDirection _transferDirection = TransferDirection::None;
        bool _lz4Transfer = false;
        size_t _maximumBlockLength = 0;

        bool EncodeAddressAndLength(std::vector<uint8_t> &payload,
                                    uint64_t address,
                                    uint64_t size,
                                    uint8_t addressLength,
                                    uint8_t sizeLength) const;
        static void AppendBigEndian(std::vector<uint8_t> &target, uint64_t value, uint8_t length);
        static size_t ParseMaximumBlockLength(const UDSResponse &response);
        UDSResponse SubFunctionRequest(uint8_t service,
                                       uint8_t subFunction,
                                       const std::vector<uint8_t> &parameters,
                                       bool suppressPositiveResponse);
};

#endif
