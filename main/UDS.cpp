#include "UDS.h"
#include "LZ4.h"

#include <algorithm>
#include <atomic>
#include <utility>

using namespace EmbeddedIOServices;

namespace
{
constexpr uint8_t negativeResponseService = 0x7F;
constexpr uint8_t responsePending = 0x78;
constexpr uint32_t pollIntervalMs = 1;
constexpr uint8_t lz4DataFormatIdentifier = 0x10;
constexpr size_t maximumLZ4BlockLength = 4096;
}

UDS::UDS(ICommunicationService *transport,
         std::function<void(uint32_t)> delay,
         uint32_t responseTimeoutMs,
         uint32_t responsePendingTimeoutMs)
    : _transport(transport),
      _delay(delay),
      _responseTimeoutMs(responseTimeoutMs),
      _responsePendingTimeoutMs(responsePendingTimeoutMs)
{
}

UDSResponse UDS::Request(uint8_t service, const std::vector<uint8_t> &payload, bool expectResponse)
{
    return Request(service, payload.data(), payload.size(), expectResponse);
}

UDSResponse UDS::Request(uint8_t service, const uint8_t *payload, size_t payloadLength, bool expectResponse)
{
    if(_transport == nullptr || !_delay || (payload == nullptr && payloadLength != 0))
        return {UDSStatus::InvalidArgument};

    std::vector<uint8_t> request;
    request.reserve(payloadLength + 1);
    request.push_back(service);
    if(payloadLength != 0)
        request.insert(request.end(), payload, payload + payloadLength);

    UDSResponse response;
    std::atomic<bool> complete(false);
    std::atomic<bool> pending(false);
    communication_receive_callback_id_t callbackId = 0;

    if(expectResponse)
    {
        callbackId = _transport->RegisterReceiveCallBack(
            [&response, &complete, &pending, service](communication_send_callback_t, const void *data, size_t length) -> size_t {
                if(complete.load(std::memory_order_acquire) || data == nullptr || length == 0)
                    return 0;

                const uint8_t *bytes = static_cast<const uint8_t *>(data);
                if(bytes[0] == static_cast<uint8_t>(service + 0x40))
                {
                    response.status = UDSStatus::Ok;
                    response.payload.assign(bytes + 1, bytes + length);
                    complete.store(true, std::memory_order_release);
                }
                else if(bytes[0] == negativeResponseService && length >= 3 && bytes[1] == service)
                {
                    if(bytes[2] == responsePending)
                    {
                        pending.store(true, std::memory_order_release);
                    }
                    else
                    {
                        response.status = UDSStatus::NegativeResponse;
                        response.negativeResponseCode = bytes[2];
                        if(length > 3)
                            response.payload.assign(bytes + 3, bytes + length);
                        complete.store(true, std::memory_order_release);
                    }
                }
                return 0;
            });
    }

    uint32_t readyWaitMs = 0;
    while(!_transport->Ready() && readyWaitMs < _responseTimeoutMs)
    {
        _delay(pollIntervalMs);
        readyWaitMs += pollIntervalMs;
    }
    if(!_transport->Ready())
    {
        if(expectResponse)
            _transport->UnRegisterReceiveCallBack(callbackId);
        return {UDSStatus::Timeout};
    }
    _transport->Send(request.data(), request.size());

    if(!expectResponse)
        return response;

    uint32_t elapsedMs = 0;
    uint32_t timeoutMs = _responseTimeoutMs;
    while(!complete.load(std::memory_order_acquire))
    {
        _delay(pollIntervalMs);
        elapsedMs += pollIntervalMs;
        if(pending.exchange(false, std::memory_order_acq_rel))
        {
            elapsedMs = 0;
            timeoutMs = _responsePendingTimeoutMs;
        }
        if(elapsedMs >= timeoutMs)
        {
            response.status = UDSStatus::Timeout;
            break;
        }
    }

    _transport->UnRegisterReceiveCallBack(callbackId);
    return response;
}

UDSResponse UDS::SubFunctionRequest(uint8_t service, uint8_t subFunction, const std::vector<uint8_t> &parameters, bool suppressPositiveResponse)
{
    std::vector<uint8_t> payload;
    payload.reserve(parameters.size() + 1);
    payload.push_back(subFunction | (suppressPositiveResponse ? 0x80 : 0x00));
    payload.insert(payload.end(), parameters.begin(), parameters.end());
    return Request(service, payload, !suppressPositiveResponse);
}

void UDS::AppendBigEndian(std::vector<uint8_t> &target, uint64_t value, uint8_t length)
{
    for(uint8_t i = length; i > 0; --i)
        target.push_back(static_cast<uint8_t>(value >> ((i - 1) * 8)));
}

bool UDS::EncodeAddressAndLength(std::vector<uint8_t> &payload, uint64_t address, uint64_t size, uint8_t addressLength, uint8_t sizeLength) const
{
    if(addressLength == 0 || addressLength > 8 || sizeLength == 0 || sizeLength > 8)
        return false;
    if((addressLength < 8 && address >= (uint64_t{1} << (addressLength * 8))) ||
       (sizeLength < 8 && size >= (uint64_t{1} << (sizeLength * 8))))
        return false;

    payload.push_back(static_cast<uint8_t>((sizeLength << 4) | addressLength));
    AppendBigEndian(payload, address, addressLength);
    AppendBigEndian(payload, size, sizeLength);
    return true;
}

UDSResponse UDS::DiagnosticSessionControl(uint8_t session, bool suppress) { return SubFunctionRequest(0x10, session, {}, suppress); }
UDSResponse UDS::ECUReset(uint8_t resetType, bool suppress) { return SubFunctionRequest(0x11, resetType, {}, suppress); }

UDSResponse UDS::ClearDiagnosticInformation(uint32_t dtcGroup)
{
    std::vector<uint8_t> payload;
    AppendBigEndian(payload, dtcGroup, 3);
    return Request(0x14, payload);
}

UDSResponse UDS::ReadDTCInformation(uint8_t subFunction, const std::vector<uint8_t> &parameters) { return SubFunctionRequest(0x19, subFunction, parameters, false); }

UDSResponse UDS::ReadDataByIdentifier(const std::vector<uint16_t> &identifiers)
{
    if(identifiers.empty()) return {UDSStatus::InvalidArgument};
    std::vector<uint8_t> payload;
    payload.reserve(identifiers.size() * 2);
    for(uint16_t identifier : identifiers) AppendBigEndian(payload, identifier, 2);
    return Request(0x22, payload);
}

UDSResponse UDS::ReadMemoryByAddress(uint64_t address, uint64_t size, uint8_t addressLength, uint8_t sizeLength)
{
    std::vector<uint8_t> payload;
    if(!EncodeAddressAndLength(payload, address, size, addressLength, sizeLength)) return {UDSStatus::InvalidArgument};
    return Request(0x23, payload);
}

UDSResponse UDS::ReadScalingDataByIdentifier(uint16_t identifier)
{
    std::vector<uint8_t> payload;
    AppendBigEndian(payload, identifier, 2);
    return Request(0x24, payload);
}

UDSResponse UDS::SecurityAccessRequestSeed(uint8_t level, const std::vector<uint8_t> &parameters) { return SubFunctionRequest(0x27, level, parameters, false); }
UDSResponse UDS::SecurityAccessSendKey(uint8_t level, const std::vector<uint8_t> &key) { return SubFunctionRequest(0x27, level, key, false); }

UDSResponse UDS::CommunicationControl(uint8_t controlType, uint8_t communicationType, const std::vector<uint8_t> &nodeIdentification, bool suppress)
{
    std::vector<uint8_t> parameters{communicationType};
    parameters.insert(parameters.end(), nodeIdentification.begin(), nodeIdentification.end());
    return SubFunctionRequest(0x28, controlType, parameters, suppress);
}

UDSResponse UDS::Authentication(uint8_t subFunction, const std::vector<uint8_t> &parameters, bool suppress) { return SubFunctionRequest(0x29, subFunction, parameters, suppress); }

UDSResponse UDS::ReadDataByPeriodicIdentifier(uint8_t transmissionMode, const std::vector<uint8_t> &identifiers)
{
    if(identifiers.empty()) return {UDSStatus::InvalidArgument};
    std::vector<uint8_t> payload{transmissionMode};
    payload.insert(payload.end(), identifiers.begin(), identifiers.end());
    return Request(0x2A, payload);
}

UDSResponse UDS::DynamicallyDefineDataIdentifier(uint8_t subFunction, const std::vector<uint8_t> &definition) { return SubFunctionRequest(0x2C, subFunction, definition, false); }

UDSResponse UDS::TesterPresent(bool suppress) { return SubFunctionRequest(0x3E, 0x00, {}, suppress); }
UDSResponse UDS::AccessTimingParameter(uint8_t subFunction, const std::vector<uint8_t> &parameters) { return SubFunctionRequest(0x83, subFunction, parameters, false); }
UDSResponse UDS::SecuredDataTransmission(const std::vector<uint8_t> &data) { return Request(0x84, data); }
UDSResponse UDS::ControlDTCSetting(uint8_t settingType, const std::vector<uint8_t> &options, bool suppress) { return SubFunctionRequest(0x85, settingType, options, suppress); }
UDSResponse UDS::ResponseOnEvent(uint8_t eventType, const std::vector<uint8_t> &window, bool suppress) { return SubFunctionRequest(0x86, eventType, window, suppress); }
UDSResponse UDS::LinkControl(uint8_t controlType, const std::vector<uint8_t> &parameters, bool suppress) { return SubFunctionRequest(0x87, controlType, parameters, suppress); }

UDSResponse UDS::WriteDataByIdentifier(uint16_t identifier, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> payload;
    AppendBigEndian(payload, identifier, 2);
    payload.insert(payload.end(), data.begin(), data.end());
    return Request(0x2E, payload);
}

UDSResponse UDS::InputOutputControlByIdentifier(uint16_t identifier, uint8_t controlParameter, const std::vector<uint8_t> &controlState)
{
    std::vector<uint8_t> payload;
    AppendBigEndian(payload, identifier, 2);
    payload.push_back(controlParameter);
    payload.insert(payload.end(), controlState.begin(), controlState.end());
    return Request(0x2F, payload);
}

UDSResponse UDS::RoutineControl(uint8_t routineType, uint16_t routineIdentifier, const std::vector<uint8_t> &optionRecord)
{
    std::vector<uint8_t> parameters;
    AppendBigEndian(parameters, routineIdentifier, 2);
    parameters.insert(parameters.end(), optionRecord.begin(), optionRecord.end());
    return SubFunctionRequest(0x31, routineType, parameters, false);
}

UDSResponse UDS::RequestDownload(uint64_t address, uint64_t size, uint8_t dataFormatIdentifier, uint8_t addressLength, uint8_t sizeLength)
{
    std::vector<uint8_t> payload{dataFormatIdentifier};
    if(!EncodeAddressAndLength(payload, address, size, addressLength, sizeLength)) return {UDSStatus::InvalidArgument};
    UDSResponse response = Request(0x34, payload);
    bool usingLZ4 = dataFormatIdentifier == lz4DataFormatIdentifier;
    if(!response.Ok() && usingLZ4)
    {
        payload[0] = 0x00;
        response = Request(0x34, payload);
        usingLZ4 = false;
    }
    _transferDirection = response.Ok() ? TransferDirection::Download : TransferDirection::None;
    _lz4Transfer = response.Ok() && usingLZ4;
    _maximumBlockLength = response.Ok() ? ParseMaximumBlockLength(response) : 0;
    return response;
}

UDSResponse UDS::RequestUpload(uint64_t address, uint64_t size, uint8_t dataFormatIdentifier, uint8_t addressLength, uint8_t sizeLength)
{
    std::vector<uint8_t> payload{dataFormatIdentifier};
    if(!EncodeAddressAndLength(payload, address, size, addressLength, sizeLength)) return {UDSStatus::InvalidArgument};
    UDSResponse response = Request(0x35, payload);
    bool usingLZ4 = dataFormatIdentifier == lz4DataFormatIdentifier;
    if(!response.Ok() && usingLZ4)
    {
        payload[0] = 0x00;
        response = Request(0x35, payload);
        usingLZ4 = false;
    }
    _transferDirection = response.Ok() ? TransferDirection::Upload : TransferDirection::None;
    _lz4Transfer = response.Ok() && usingLZ4;
    _maximumBlockLength = response.Ok() ? ParseMaximumBlockLength(response) : 0;
    return response;
}

UDSResponse UDS::TransferData(uint8_t blockSequenceCounter, const std::vector<uint8_t> &data)
{
    if(_lz4Transfer && _transferDirection == TransferDirection::Download)
    {
        if(data.empty() || data.size() > maximumLZ4BlockLength || _maximumBlockLength <= 4)
            return {UDSStatus::InvalidArgument};

        std::vector<uint8_t> payload{
            blockSequenceCounter,
            static_cast<uint8_t>(data.size() >> 8),
            static_cast<uint8_t>(data.size())
        };
        const size_t compressedCapacity = _maximumBlockLength - 4;
        payload.resize(3 + compressedCapacity);
        size_t compressedLength = compressedCapacity;
        if(Kernel::EncodeLZ4Block(data.data(), data.size(), payload.data() + 3,
                                  compressedLength) != Kernel::LZ4EncodeResult::Success)
            return {UDSStatus::CompressionError};
        payload.resize(3 + compressedLength);
        return Request(0x36, payload);
    }

    std::vector<uint8_t> payload{blockSequenceCounter};
    payload.insert(payload.end(), data.begin(), data.end());
    UDSResponse response = Request(0x36, payload);
    if(!_lz4Transfer || _transferDirection != TransferDirection::Upload || !response.Ok())
        return response;

    if(response.payload.size() < 4 || response.payload[0] != blockSequenceCounter)
        return {UDSStatus::CompressionError};
    const size_t decodedLength =
        (static_cast<size_t>(response.payload[1]) << 8) | response.payload[2];
    if(decodedLength == 0 || decodedLength > maximumLZ4BlockLength)
        return {UDSStatus::CompressionError};

    std::vector<uint8_t> decoded(decodedLength + 1);
    decoded[0] = blockSequenceCounter;
    if(Kernel::DecodeLZ4Block(response.payload.data() + 3,
                              response.payload.size() - 3,
                              decoded.data() + 1,
                              decodedLength) != Kernel::LZ4DecodeResult::Success)
        return {UDSStatus::CompressionError};
    response.payload = std::move(decoded);
    return response;
}

UDSResponse UDS::RequestTransferExit(const std::vector<uint8_t> &parameters)
{
    UDSResponse response = Request(0x37, parameters);
    _transferDirection = TransferDirection::None;
    _lz4Transfer = false;
    _maximumBlockLength = 0;
    return response;
}

size_t UDS::ParseMaximumBlockLength(const UDSResponse &response)
{
    if(response.payload.empty()) return 0;
    const uint8_t lengthBytes = response.payload[0] >> 4;
    if(lengthBytes == 0 || lengthBytes > sizeof(size_t) ||
       response.payload.size() < static_cast<size_t>(lengthBytes) + 1)
        return 0;
    size_t result = 0;
    for(uint8_t i = 0; i < lengthBytes; ++i)
        result = (result << 8) | response.payload[i + 1];
    return result;
}

UDSResponse UDS::RequestFileTransfer(uint8_t modeOfOperation, const std::vector<uint8_t> &parameters)
{
    std::vector<uint8_t> payload{modeOfOperation};
    payload.insert(payload.end(), parameters.begin(), parameters.end());
    return Request(0x38, payload);
}

UDSResponse UDS::WriteMemoryByAddress(uint64_t address, const std::vector<uint8_t> &data, uint8_t addressLength, uint8_t sizeLength)
{
    if(data.empty()) return {UDSStatus::InvalidArgument};
    std::vector<uint8_t> payload;
    if(!EncodeAddressAndLength(payload, address, data.size(), addressLength, sizeLength)) return {UDSStatus::InvalidArgument};
    payload.insert(payload.end(), data.begin(), data.end());
    return Request(0x3D, payload);
}
