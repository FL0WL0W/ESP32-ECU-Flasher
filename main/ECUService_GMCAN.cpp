#include "ECUService_GMCAN.h"
#include <cstring>
#include <algorithm>
#include <esp_log.h>

using namespace EmbeddedIOServices;

ECUService_GMCAN::ECUService_GMCAN(ICommunicationService *ecuISOTPService, std::function<void(uint32_t)> delay)
    : _ecuISOTPService(ecuISOTPService), _delay(delay)
{
}

ECUService_GMCAN::~ECUService_GMCAN()
{
}

void ECUService_GMCAN::WaitForReadyAndSend(uint8_t *data, size_t length, uint32_t timeoutMs)
{
    // Wait for the ISOTP service to be ready before sending the next message
    uint8_t timeoutCounter = 0;
    while(!_ecuISOTPService->Ready())
    {
        _delay(10); // Delay for 10 milliseconds before checking again
        timeoutCounter++;
        if(timeoutCounter > timeoutMs / 10) // Timeout after specified milliseconds and send anyway
        {
            break;
        }
    }
    
    _ecuISOTPService->Send(data, length);
}

ECUServiceErrorCode ECUService_GMCAN::SendAndWaitForResponse(uint8_t *data, size_t length, std::function<bool(const void *data, size_t length, ECUServiceErrorCode *errorCode)> responseCondition, uint32_t timeoutMs)
{
    bool responseReady = false;
    ECUServiceErrorCode response = ECU_SERVICE_ERROR_NONE;
    communication_receive_callback_id_t callbackId = _ecuISOTPService->RegisterReceiveCallBack([&responseReady, &response, responseCondition](communication_send_callback_t send, const void *data, size_t length) -> size_t {
        if(responseReady)
        {
            return 0; // If response is already ready, do not process further
        }
        responseReady = responseCondition(data, length, &response);
        return 0; // Return 0 to indicate that we have processed the data
    });

    if(length > 0)
        WaitForReadyAndSend(data, length);

    uint8_t timeoutCounter = 0;
    while(responseReady == false)
    {
        _delay(10);
        timeoutCounter++;
        if(timeoutCounter > timeoutMs / 10) // Timeout after specified milliseconds
        {
            _ecuISOTPService->UnRegisterReceiveCallBack(callbackId);
            return ECU_SERVICE_ERROR_TIMEOUT;
        }
    }

    _ecuISOTPService->UnRegisterReceiveCallBack(callbackId);
    return response;
}

ECUServiceErrorCode ECUService_GMCAN::Connect()
{
    volatile uint32_t queryReadFlags = 0;
    uint32_t osid = 0; // Buffer to hold the OSID
    communication_receive_callback_id_t callbackId = _ecuISOTPService->RegisterReceiveCallBack([&osid, &queryReadFlags](communication_send_callback_t send, const void *data, size_t length) -> size_t {
        const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
        if(length < 2)
            return 0;
        if(dataBytes[0] == 0x5A)
        {
            if(dataBytes[1] == 0xC9)
            {
                if(length < 6)
                {
                    queryReadFlags |= 0b10000000000000000000000000000000; //bad response, set the last bit to indicate that the osid read failed
                    return 0;
                }
                osid = (static_cast<uint32_t>(dataBytes[2]) << 24);
                osid |= (static_cast<uint32_t>(dataBytes[3]) << 16);
                osid |= (static_cast<uint32_t>(dataBytes[4]) << 8);
                osid |= static_cast<uint32_t>(dataBytes[5]);
                queryReadFlags |= 0b00000000000000000000000000000001; // Set the first bit to indicate that the osid has been read
            }
        }
        return 0;
    });
    uint8_t cmdOSID[] = {0x1A, 0xC9};
    WaitForReadyAndSend(cmdOSID, 2);

    uint8_t timeoutCounter = 0;
    while(queryReadFlags != 0b00000000000000000000000000000001)
    {
        _delay(10);
        timeoutCounter++;
        if(queryReadFlags & 0b10000000000000000000000000000000) // Check if the last bit is set indicating a bad response
        {
            _ecuISOTPService->UnRegisterReceiveCallBack(callbackId);
            return ECU_SERVICE_ERROR_BAD_RESPONSE; // Return a specific error code for bad response
        }
        if(timeoutCounter > 50) // Timeout after 500 milliseconds
        {
            _ecuISOTPService->UnRegisterReceiveCallBack(callbackId);
            return ECU_SERVICE_ERROR_TIMEOUT;
        }
    }

    _ecuISOTPService->UnRegisterReceiveCallBack(callbackId);
    return ECU_SERVICE_ERROR_NONE;
}

ECUServiceErrorCode ECUService_GMCAN::Disconnect()
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode ECUService_GMCAN::SuppressBroadcastMessages(bool suppress)
{
    if(suppress)
    {
        uint8_t cmdSuppressBroadcastMessages[] = {0x28};
        return SendAndWaitForResponse(cmdSuppressBroadcastMessages, 1, [](const void *data, size_t length, ECUServiceErrorCode *errorCode) {
            const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
            if(length > 0 &&dataBytes[0] == 0x68)
            {
                return true; // Response received
            }
            return false; // Continue waiting for the response
        });
    }
    
    return ECU_SERVICE_ERROR_NONE;
}

ECUServiceErrorCode ECUService_GMCAN::QueryInformation(ECUInformation *info)
{
    uint32_t queryReadFlags = 0;
    uint8_t vin[17] = {0}; // Buffer to hold the VIN
    communication_receive_callback_id_t callbackId = _ecuISOTPService->RegisterReceiveCallBack([&vin, &queryReadFlags](communication_send_callback_t send, const void *data, size_t length) -> size_t {
        const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
        if(length < 2)
            return 0;
        if(dataBytes[0] == 0x7C)
        {
            if(dataBytes[1] == 0x01)
            {
                std::memcpy(vin, dataBytes + 2, std::min((size_t)5, length - 2)); // Copy the first 5 bytes of the VIN into info->ECUVin
                queryReadFlags |= 0b00000000000000000000000000000001; // Set the first bit to indicate that the first part of the VIN has been read
            }
            else if(dataBytes[1] == 0x02)
            {
                std::memcpy(vin + 5, dataBytes + 2, std::min((size_t)6, length - 2)); // Copy the next 6 bytes of the VIN into info->ECUVin
                queryReadFlags |= 0b00000000000000000000000000000010; // Set the second bit to indicate that the second part of the VIN has been read
            }
            else if(dataBytes[1] == 0x03)
            {
                std::memcpy(vin + 11, dataBytes + 2, std::min((size_t)6, length - 2)); // Copy the last 6 bytes of the VIN into info->ECUVin
                queryReadFlags |= 0b00000000000000000000000000000100; // Set the third bit to indicate that the last part of the VIN has been read
            }
        }
        return length;
    });
    uint8_t cmdVIN1[2] = {0x3C, 0x01};
    uint8_t cmdVIN2[2] = {0x3C, 0x02};
    uint8_t cmdVIN3[2] = {0x3C, 0x03};
    WaitForReadyAndSend(cmdVIN1, sizeof(cmdVIN1));
    WaitForReadyAndSend(cmdVIN2, sizeof(cmdVIN2));
    WaitForReadyAndSend(cmdVIN3, sizeof(cmdVIN3));

    uint8_t timeoutCounter = 0;
    while(queryReadFlags != 0b00000000000000000000000000000111)
    {
        _delay(10);
        timeoutCounter++;
        if(timeoutCounter > 50) // Timeout after 500 milliseconds
        {
            _ecuISOTPService->UnRegisterReceiveCallBack(callbackId);
            return ECU_SERVICE_ERROR_TIMEOUT;
        }
    }

    std::memcpy(info->ECUVin, vin, 17);

    _ecuISOTPService->UnRegisterReceiveCallBack(callbackId);
    return ECU_SERVICE_ERROR_NONE;
}

ECUServiceErrorCode ECUService_GMCAN::IncreaseCommunicationSpeed()
{
    return ECU_SERVICE_ERROR_NONE; //no method to increase speed on GMCAN, just return success
}

ECUServiceErrorCode ECUService_GMCAN::DecreaseCommunicationSpeed()
{
    return ECU_SERVICE_ERROR_NONE; //no method to decrease speed on GMCAN, just return success
}

ECUServiceErrorCode ECUService_GMCAN::UnlockECU()
{
    ECUServiceErrorCode errorCode = ECU_SERVICE_ERROR_NONE;

    uint8_t cmdRequestSeed[] = {0x27, 0x01};
    uint8_t seed[5] = {0}; // Buffer to hold the seed
    uint8_t seedLength = 0; // Variable to hold the seed length
    errorCode = SendAndWaitForResponse(cmdRequestSeed, 2, [&seed, &seedLength](const void *data, size_t length, ECUServiceErrorCode *errorCode) {
        const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
        if(length < 2)
            return false;
        if(dataBytes[0] == 0x67)
        {
            if(dataBytes[1] == 0x01) //response to seed request
            {
                if(length < 4)
                {
                    *errorCode = ECU_SERVICE_ERROR_BAD_RESPONSE; //bad response
                    return true;
                }
                if(length == 4)
                {
                    seed[0] = static_cast<uint32_t>(dataBytes[2]);
                    seed[1] = static_cast<uint32_t>(dataBytes[3]);
                    seedLength = 2;
                    return true;
                }
                if(length == 7)
                {
                    seed[0] = static_cast<uint32_t>(dataBytes[2]);
                    seed[1] = static_cast<uint32_t>(dataBytes[3]);
                    seed[2] = static_cast<uint32_t>(dataBytes[4]);
                    seed[3] = static_cast<uint32_t>(dataBytes[5]);
                    seed[4] = static_cast<uint32_t>(dataBytes[6]);
                    seedLength = 5;
                    return true;
                }
            }
        }
        if(dataBytes[0] == 0x7F) //negative response to seed request
        {
            if(dataBytes[1] == 0x27)
            {
                *errorCode = ECU_SERVICE_ERROR_BAD_RESPONSE; //bad response
                return true;
            }
        }
        return false; // Continue waiting for the response
    });

    if(errorCode != ECU_SERVICE_ERROR_NONE) return errorCode; // Return if error occurred during seed request
    
    if(seed[0] == 0x00 && seed[1] == 0x00 && seed[2] == 0x00 && seed[3] == 0x00 && seed[4] == 0x00) //already unlocked, no need to send key
    {
        return errorCode;
    }

    uint8_t key[5] = {0}; // Buffer to hold the key
    errorCode = SeedKeyAlgorithm(key, seed, seedLength);

    if(errorCode != ECU_SERVICE_ERROR_NONE) return errorCode; // Return if error occurred during generating key

    uint8_t cmdSendKey[] = {0x27, 0x02, key[0], key[1], key[2], key[3], key[4]};
    errorCode = SendAndWaitForResponse(cmdSendKey, 2 + seedLength, [&seed, &seedLength](const void *data, size_t length, ECUServiceErrorCode *errorCode) {
        const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
        if(length < 2)
            return false;
        if(dataBytes[0] == 0x67)
        {
            if(dataBytes[1] == 0x02) //response to send key
            {
                return true; // Response received
            }
        }
        if(dataBytes[0] == 0x7F) //negative response to key
        {
            if(dataBytes[1] == 0x27)
            {
                *errorCode = ECU_SERVICE_ERROR_BAD_KEY; //bad response
                return true;
            }
        }
        return false; // Continue waiting for the response
    });

    return errorCode;
}

ECUServiceErrorCode ECUService_GMCAN::LoadKernel()
{
    ECUServiceErrorCode errorCode = ECU_SERVICE_ERROR_NONE;

    uint8_t cmdProgrammingSession[] = {0x10, 0x02};
    errorCode = SendAndWaitForResponse(cmdProgrammingSession, 2, [](const void *data, size_t length, ECUServiceErrorCode *errorCode) {
        const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
        if(length < 1)
            return false;
        if(dataBytes[0] == 0x50)
        {
            return true; // Response received
        }
        if(dataBytes[0] == 0x7f) //negative response
        {
            if(length < 2)
                return false;
            if(dataBytes[1] == 0x10)
            {
                *errorCode = ECU_SERVICE_ERROR_BAD_RESPONSE; //bad response
                return true;
            }
        }
        return false; // Continue waiting for the response
    });

    if(errorCode != ECU_SERVICE_ERROR_NONE) return errorCode; // Return if error occurred during programming session request

    uint8_t cmdPrepareProgramming[] = {0xA5, 0x01};
    errorCode = SendAndWaitForResponse(cmdPrepareProgramming, 2, [](const void *data, size_t length, ECUServiceErrorCode *errorCode) {
        const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
        if(length < 1)
            return false;
        if(dataBytes[0] == 0xe5)
        {
            return true; // Response received
        }
        if(dataBytes[0] == 0x7f) //negative response
        {
            if(length < 2)
                return false;
            if(dataBytes[1] == 0xa5)
            {
                *errorCode = ECU_SERVICE_ERROR_BAD_RESPONSE; //bad response
                return true;
            }
        }
        return false; // Continue waiting for the response
    });

    if(errorCode != ECU_SERVICE_ERROR_NONE) return errorCode; // Return if error occurred during prepare programming request

    uint8_t cmdCommitProgramming[] = {0xA5, 0x03};
    errorCode = SendAndWaitForResponse(cmdCommitProgramming, 2, [](const void *data, size_t length, ECUServiceErrorCode *errorCode) {
        const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
        if(length < 1)
            return false;
        if(dataBytes[0] == 0xe5)
        {
            return true; // Response received
        }
        if(dataBytes[0] == 0x7f) //negative response
        {
            if(length < 2)
                return false;
            if(dataBytes[1] == 0xa5)
            {
                *errorCode = ECU_SERVICE_ERROR_BAD_RESPONSE; //bad response
                return true;
            }
        }
        return false; // Continue waiting for the response
    });

    if(errorCode != ECU_SERVICE_ERROR_NONE && errorCode != ECU_SERVICE_ERROR_TIMEOUT) return errorCode; // Return if error occurred during commit programming request unless timeout since this usually doesn't have a response
    errorCode = ECU_SERVICE_ERROR_NONE; // Reset error code to none since we are proceeding with the kernel load

    const uint32_t blockSize = 2048 - 6; // 2048 bytes minus the 6 bytes for the load command
    const uint32_t totalBlocks = _kernelSize / blockSize; // Calculate the total number of blocks needed to send the kernel data
    const uint32_t remainingBytes = _kernelSize % blockSize; // Calculate the remaining bytes that don't fit into a full block
    const uint32_t totalSizeWithHeaders = totalBlocks * (blockSize + 6) + (remainingBytes > 0 ? remainingBytes + 6 : 0); // Calculate the total size of the kernel data and headers to be sent
    uint32_t offset = totalBlocks * blockSize; // Calculate the offset for the remaining bytes

    uint8_t cmdRequestDownload[] = {0x34, 0x00, static_cast<uint8_t>(totalSizeWithHeaders >> 16), static_cast<uint8_t>(totalSizeWithHeaders >> 8), static_cast<uint8_t>(totalSizeWithHeaders)};
    errorCode = SendAndWaitForResponse(cmdRequestDownload, 5, [](const void *data, size_t length, ECUServiceErrorCode *errorCode) {
        const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
        if(length < 1)
            return false;
        if(dataBytes[0] == 0x74)
        {
            return true; // Response received
        }
        if(dataBytes[0] == 0x7f) //negative response
        {
            if(length < 3)
                return false;
            if(dataBytes[1] == 0x34) //response to wait for request download
            {
                if(dataBytes[2] == 0x78)
                {
                    return false; // wait response
                }
                else
                {
                    *errorCode = ECU_SERVICE_ERROR_BAD_RESPONSE; //bad response
                    return true;
                }
            }
        }
        return false; // Continue waiting for the response
    });
    
    if(errorCode != ECU_SERVICE_ERROR_NONE) return errorCode; // Return if error occurred during download request

    uint8_t cmdTransferData[6 + blockSize] = { 
        0x36, // TransferData command
        0x00 
    };
    auto transferDataResponseCondition = [](const void *data, size_t length, ECUServiceErrorCode *errorCode) {
        const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
        if(length < 1)
            return false;
        if(dataBytes[0] == 0x76)
        {
            return true; // Response received
        }
        if(dataBytes[0] == 0x7f) //negative response
        {
            if(length < 3)
                return false;
            if(dataBytes[1] == 0x36) //response to transfer data
            {
                *errorCode = ECU_SERVICE_ERROR_BAD_RESPONSE; //bad response
                return true;
            }
        }
        return false; // Continue waiting for the response
    };

    if(remainingBytes > 0)
    {
        const uint32_t loadAddress = _kernelLoadAddress + offset;
        cmdTransferData[2] = static_cast<uint8_t>(loadAddress >> 24);
        cmdTransferData[3] = static_cast<uint8_t>(loadAddress >> 16);
        cmdTransferData[4] = static_cast<uint8_t>(loadAddress >> 8);
        cmdTransferData[5] = static_cast<uint8_t>(loadAddress);
        std::memcpy(cmdTransferData + 6, _kernelData + offset, remainingBytes);
        errorCode = SendAndWaitForResponse(cmdTransferData, remainingBytes + 6, transferDataResponseCondition);
        if(errorCode != ECU_SERVICE_ERROR_NONE) return errorCode; // Return if error occurred during transfer data request
    }

    for(uint32_t i = totalBlocks; i > 0; i--)
    {
        offset -= blockSize;
        const uint32_t loadAddress = _kernelLoadAddress + offset;
        cmdTransferData[2] = static_cast<uint8_t>(loadAddress >> 24);
        cmdTransferData[3] = static_cast<uint8_t>(loadAddress >> 16);
        cmdTransferData[4] = static_cast<uint8_t>(loadAddress >> 8);
        cmdTransferData[5] = static_cast<uint8_t>(loadAddress);
        std::memcpy(cmdTransferData + 6, _kernelData + offset, blockSize);
        errorCode = SendAndWaitForResponse(cmdTransferData, blockSize + 6, transferDataResponseCondition);
        if(errorCode != ECU_SERVICE_ERROR_NONE) return errorCode; // Return if error occurred during transfer data request
    }

    if(_kernelRunAddress == 0x00)
    {
        return errorCode; // Return if kernel run address is not set, no need to execute
    }

    cmdTransferData[1] = 0x80; // Set the second byte to 0x80 for execute
    cmdTransferData[2] = static_cast<uint8_t>(_kernelRunAddress >> 24);
    cmdTransferData[3] = static_cast<uint8_t>(_kernelRunAddress >> 16);
    cmdTransferData[4] = static_cast<uint8_t>(_kernelRunAddress >> 8);
    cmdTransferData[5] = static_cast<uint8_t>(_kernelRunAddress);
    errorCode = SendAndWaitForResponse(cmdTransferData, 6, [](const void *data, size_t length, ECUServiceErrorCode *errorCode) {
        const uint8_t *dataBytes = reinterpret_cast<const uint8_t *>(data);
        if(length < 1)
            return false;
        if(dataBytes[0] == 0x99)
        {
            return true; // Response received
        }
        if(dataBytes[0] == 0x7f) //negative response
        {
            if(length < 3)
                return false;
            if(dataBytes[1] == 0x36) //response to transfer data
            {
                *errorCode = ECU_SERVICE_ERROR_BAD_RESPONSE; //bad response
                return true;
            }
        }
        return false; // Continue waiting for the response
    });

    return errorCode; // return the final error code after attempting to load the kernel
}

ECUServiceErrorCode ECUService_GMCAN::ReadMemory(uint32_t address, uint8_t *buffer, uint32_t length)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode ECUService_GMCAN::EraseMemory(uint32_t address, uint32_t length)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode ECUService_GMCAN::WriteMemory(uint32_t address, const uint8_t *buffer, uint32_t length)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode ECUService_GMCAN::VerifyMemory(uint32_t address, uint32_t length, uint32_t crc)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode ECUService_GMCAN::ResetECU()
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}

ECUServiceErrorCode ECUService_GMCAN::SeedKeyAlgorithm(uint8_t *key, const uint8_t *seed, uint8_t seedLength)
{
    return ECU_SERVICE_ERROR_FUNCTION_UNDEFINED;
}