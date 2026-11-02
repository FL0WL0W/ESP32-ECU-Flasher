#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "Esp32IdfAnalogService.h"
#include "Esp32IdfDigitalService.h"
#include "Esp32IdfTimerService.h"
#include "Esp32IdfPwmService.h"
#include "Esp32IdfCANService.h"
#include "Esp32IdfCommunicationService_Socket.h"
#include "Esp32IdfCommunicationService_WebSocket.h"
#include "http_server.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include "mount.h"
#include "ECUService_E92.h"
#include "ECUService_E38.h"
#include "ECUService_E78.h"
#include "ECUService_E78.h"
#include "UDS.h"

using namespace EmbeddedIOServices;
using namespace Esp32;

#define NVS_WIFI_NAMESPACE "wifi_config"
#define NVS_WIFI_SSID_KEY "ssid"
#define NVS_WIFI_PASSWORD_KEY "password"
#define NVS_WIFI_AUTHMODE_KEY "authmode"
// Load WiFi config from NVS, fallback to defaults if not found
static void load_wifi_config_from_nvs(wifi_config_t *wifi_config)
{
    nvs_handle_t nvs_handle;    
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    
    size_t ssid_len = sizeof(wifi_config->ap.ssid);
    size_t password_len = sizeof(wifi_config->ap.password);

    if (err == ESP_OK) {
        // Try to read SSID
        if (nvs_get_str(nvs_handle, NVS_WIFI_SSID_KEY, (char *)wifi_config->ap.ssid, &ssid_len) != ESP_OK) {
            // Use default SSID with unique chip ID (MAC address)
            uint8_t mac[6];
            esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
            snprintf((char *)wifi_config->ap.ssid, sizeof(wifi_config->ap.ssid), 
                        "ECU-Flasher-%02X%02X%02X", mac[3], mac[4], mac[5]);
            nvs_set_str(nvs_handle, NVS_WIFI_SSID_KEY, (char *)wifi_config->ap.ssid);
            nvs_commit(nvs_handle);
        }
        
        // Try to read password
        nvs_get_str(nvs_handle, NVS_WIFI_PASSWORD_KEY, (char *)wifi_config->ap.password, &password_len);
        
        // Try to read authmode
        if (nvs_get_u8(nvs_handle, NVS_WIFI_AUTHMODE_KEY, (uint8_t *)&wifi_config->ap.authmode) != ESP_OK) {
            // Use default authmode (set below)
            wifi_config->ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        }
        
        nvs_close(nvs_handle);
    } else {
        // NVS not available, use defaults
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
        snprintf((char *)wifi_config->ap.ssid, sizeof(wifi_config->ap.ssid), 
                    "EFIGenie-Expander-%02X%02X%02X", mac[3], mac[4], mac[5]);
        wifi_config->ap.authmode = WIFI_AUTH_OPEN;
    }

    wifi_config->ap.authmode = (strlen((char *)wifi_config->ap.password) > 0) ? wifi_config->ap.authmode : WIFI_AUTH_OPEN;
    
    wifi_config->ap.ssid_len = strlen((char *)wifi_config->ap.ssid);
    wifi_config->ap.max_connection = 5;
}

void wifi_init_softap()
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    load_wifi_config_from_nvs(&wifi_config);
    wifi_config.ap.pmf_cfg.required = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

ICANService *_canService;
Esp32IdfCANService *_esp32CANService;
ICommunicationService *_ecuISOTPService;
ICommunicationService *_ecuWebSocketService;

namespace
{
constexpr size_t ecuFlashStagingBufferSize = 128U * 1024U;
constexpr size_t ecuTransferBufferSize = 4096U;
std::array<uint8_t, ecuFlashStagingBufferSize> ecuFlashStagingBuffer{};
std::array<uint8_t, ecuTransferBufferSize> ecuTransferBuffer{};
}

extern "C" void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //initialize netlibs
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //initialize wifi
    wifi_init_softap();

    //start http server
    start_http_server();

    //reset handler
    const httpd_uri_t resetPost = {
        .uri       = "/command/reset",
        .method     = HTTP_POST,
        .handler    = [](httpd_req_t *req) 
        {
            esp_restart();

            return ESP_OK;
        }
    };
    httpd_register_uri_handler(server, &resetPost);

    const httpd_uri_t resetToOTAUpdaterPost = {
        .uri       = "/command/resetToOTAUpdater",
        .method     = HTTP_POST,
        .handler    = [](httpd_req_t *req) 
        {
            // set the boot partition to factory and restart
            const esp_partition_t *factory_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
            if (factory_partition != NULL) {
                esp_err_t err = esp_ota_set_boot_partition(factory_partition);
                if (err != ESP_OK) {
                    ESP_LOGE("OTA", "Failed to set boot partition: %s", esp_err_to_name(err));
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition");
                } else {
                    ESP_LOGI("OTA", "Boot partition set to factory. Restarting...");
                    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    esp_restart();
                }
            } else {
                ESP_LOGE("OTA", "Factory partition not found!");
                httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Factory partition not found");
            }

            return ESP_OK;
        }
    };
    httpd_register_uri_handler(server, &resetToOTAUpdaterPost);

    static Esp32IdfCANServiceChannelConfig canconfigs[2] 
    {
        {
            .enabled = true,
            .t_config = TWAI_TIMING_CONFIG_500KBITS(),
            .f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(),
            .g_config = TWAI_GENERAL_CONFIG_DEFAULT_V2(0, (gpio_num_t)9, (gpio_num_t)8, TWAI_MODE_NORMAL)
        },
        {
            .enabled = true,
            .t_config = TWAI_TIMING_CONFIG_500KBITS(),
            .f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(),
            .g_config = TWAI_GENERAL_CONFIG_DEFAULT_V2(1, (gpio_num_t)3, (gpio_num_t)2, TWAI_MODE_NORMAL)
        }
    };
    canconfigs[0].g_config.rx_queue_len = 1000;
    canconfigs[0].g_config.tx_queue_len = 1000;
    canconfigs[1].g_config.rx_queue_len = 1000;
    canconfigs[1].g_config.tx_queue_len = 1000;

    _esp32CANService = new Esp32IdfCANService(canconfigs);
    _canService = _esp32CANService;

    static const CANIdentifier_t listenId = {.CANIdentifier = 0x7E8, .CANBusNumber = 0 };
    static const CANIdentifier_t transmitId = {.CANIdentifier = 0x7E0, .CANBusNumber = 0 };
    _ecuISOTPService = _canService->GetISOTPService(listenId, transmitId);

    //setup link between socket and isotp service
    _ecuISOTPService->RegisterReceiveCallBack([_ecuWebSocketService](communication_send_callback_t send, const void *data, size_t length) {
        esp_log_buffer_hex("ISOTP", data, std::min(length, (size_t)64));
        return 0;
    });

    static const CANIdentifier_t consoleListenId = {.CANIdentifier = 0x608, .CANBusNumber = 0};
    static const CANIdentifier_t consoleTransmitId = {.CANIdentifier = 0x600, .CANBusNumber = 0};
    ICommunicationService *const ecuConsole = _canService->GetISOTPService(consoleListenId, consoleTransmitId);
    ecuConsole->RegisterReceiveCallBack([](communication_send_callback_t, const void *data, size_t length) -> size_t {
        const char *const text = static_cast<const char *>(data);
        size_t printableLength = length;
        if(printableLength > 0)
            printf("%.*s", static_cast<int>(printableLength), text);
        return length;
    });

    static IECUService *ecuService = new ECUService_E78(_ecuISOTPService, [](uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); });
    static UDS *udsService = new UDS(_ecuISOTPService, [](uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); });

    const httpd_uri_t uploadKernelPost = {
        .uri       = "/command/uploadKernel",
        .method     = HTTP_POST,
        .handler    = [ecuService](httpd_req_t *req) 
        {
            constexpr size_t kernelChunkSize = 2000;
            constexpr uint32_t kernelLoadAddress = 0x40014000;
            const int total_len = req->content_len;
            if(total_len <= 0) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Kernel data is empty");
                return ESP_OK;
            }

            if(ecuService->UnlockECU() == ECU_SERVICE_ERROR_NONE) {
                ESP_LOGI("ECU", "ECU unlocked successfully");
            } else {
                ESP_LOGE("ECU", "Failed to unlock ECU");
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to unlock ECU");
                return ESP_OK;
            }

            if(ecuService->SuppressBroadcastMessages(true) == ECU_SERVICE_ERROR_NONE) {
                ESP_LOGI("ECU", "Broadcast messages suppressed successfully");
            } else {
                ESP_LOGE("ECU", "Failed to suppress broadcast messages");            }

            if(ecuService->EnableProgrammingMode() != ECU_SERVICE_ERROR_NONE) {
                ESP_LOGE("ECU", "Failed to enable programming mode");
                // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to enable programming mode");
                // return ESP_OK;
            }

            uint8_t *kernel_data = static_cast<uint8_t *>(malloc(kernelChunkSize));
            if(kernel_data == nullptr) {
                ESP_LOGE("ECU", "Failed to allocate kernel chunk buffer");
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
                return ESP_OK;
            }

            int received = 0;
            while(received < total_len) {
                const size_t bytesToRead = std::min(
                    kernelChunkSize,
                    static_cast<size_t>(total_len - received));
                size_t chunkLength = 0;

                while(chunkLength < bytesToRead) {
                    const int recv_len = httpd_req_recv(
                        req,
                        reinterpret_cast<char *>(kernel_data) + chunkLength,
                        bytesToRead - chunkLength);
                    if(recv_len <= 0) {
                        if(recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                            ESP_LOGE("ECU", "Socket timeout while reading kernel data");
                            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Socket timeout");
                        } else {
                            ESP_LOGE("ECU", "Incomplete kernel data: received %d/%d bytes", received + static_cast<int>(chunkLength), total_len);
                            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete kernel data");
                        }
                        free(kernel_data);
                        return ESP_OK;
                    }
                    chunkLength += recv_len;
                }

                const uint32_t writeAddress = kernelLoadAddress + static_cast<uint32_t>(received);
                if(ecuService->WriteMemory(writeAddress, kernel_data, chunkLength) != ECU_SERVICE_ERROR_NONE) {
                    ESP_LOGE("ECU", "Failed to write kernel chunk at 0x%08lX", static_cast<unsigned long>(writeAddress));
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write kernel to memory");
                    free(kernel_data);
                    return ESP_OK;
                }

                received += static_cast<int>(chunkLength);
                ESP_LOGI("ECU", "Wrote kernel bytes %d/%d", received, total_len);
            }

            if(ecuService->ExecuteMemory(kernelLoadAddress) == ECU_SERVICE_ERROR_NONE) {
                ESP_LOGI("ECU", "Kernel written and executed successfully (%d bytes)", total_len);
                httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
            } else {
                ESP_LOGE("ECU", "Failed to execute kernel");
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to execute kernel");
            }
            free(kernel_data);
            return ESP_OK;
        }
    };
    httpd_register_uri_handler(server, &uploadKernelPost);

    const httpd_uri_t uploadECUPost = {
        .uri       = "/command/uploadECU",
        .method     = HTTP_POST,
        .handler    = [udsService](httpd_req_t *req)
        {
            char query[96];
            char addressText[32];

            if(httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
               httpd_query_key_value(query, "address", addressText, sizeof(addressText)) != ESP_OK)
            {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "address query parameter is required");
                return ESP_OK;
            }

            errno = 0;
            char *addressEnd = nullptr;
            const unsigned long long parsedAddress = strtoull(addressText, &addressEnd, 0);
            const uint64_t lengthValue = static_cast<uint64_t>(req->content_len);
            if(errno != 0 || addressEnd == addressText || *addressEnd != '\0' ||
               parsedAddress > std::numeric_limits<uint32_t>::max() ||
               req->content_len <= 0 ||
               parsedAddress + lengthValue > (uint64_t{1} << 32))
            {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid address or empty upload");
                return ESP_OK;
            }

            std::vector<UDSFlashBlock> flashBlocks;
            UDSResponse mapResponse = udsService->GetFlashBlockMap(flashBlocks);
            if(!mapResponse.Ok())
            {
                ESP_LOGE("UDS", "Flash block map failed (status %u, NRC 0x%02X)",
                         static_cast<unsigned>(mapResponse.status),
                         mapResponse.negativeResponseCode);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to read flash block map");
                return ESP_OK;
            }

            struct UploadBlock
            {
                UDSFlashBlock flashBlock;
                uint32_t uploadedLength;
            };

            std::vector<UploadBlock> uploadBlocks;
            uint64_t expectedAddress = parsedAddress;
            uint64_t remainingUpload = lengthValue;
            uint64_t programmedLength = 0;
            for(const UDSFlashBlock &block : flashBlocks)
            {
                if(block.address != expectedAddress)
                    continue;

                const uint32_t uploadedBlockLength = static_cast<uint32_t>(
                    std::min<uint64_t>(remainingUpload, block.length));
                uploadBlocks.push_back({block, uploadedBlockLength});
                expectedAddress += block.length;
                remainingUpload -= uploadedBlockLength;
                programmedLength += block.length;
                if(remainingUpload == 0)
                    break;
            }
            if(uploadBlocks.empty() || remainingUpload != 0)
            {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                    "Upload address must begin on a physical flash block and fit the flash map");
                return ESP_OK;
            }

            if(programmedLength != lengthValue)
            {
                ESP_LOGI("ECU", "Padding upload from 0x%llX to 0x%llX bytes with erased 0xFF data",
                         static_cast<unsigned long long>(lengthValue),
                         static_cast<unsigned long long>(programmedLength));
            }

            uint64_t uploaded = 0;
            for(const UploadBlock &uploadBlock : uploadBlocks)
            {
                const UDSFlashBlock &block = uploadBlock.flashBlock;
                const size_t uploadedBlockLength = uploadBlock.uploadedLength;
                const bool blockBuffered = block.length <= ecuFlashStagingBufferSize;
                if(blockBuffered)
                {
                    std::fill_n(ecuFlashStagingBuffer.begin(), block.length, 0xFFU);
                    size_t received = 0;
                    while(received < uploadedBlockLength)
                    {
                        const int receiveLength = httpd_req_recv(
                            req,
                            reinterpret_cast<char *>(ecuFlashStagingBuffer.data() + received),
                            uploadedBlockLength - received);
                        if(receiveLength <= 0)
                        {
                            if(receiveLength == HTTPD_SOCK_ERR_TIMEOUT)
                                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Socket timeout");
                            else
                                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete upload");
                            return ESP_OK;
                        }
                        received += static_cast<size_t>(receiveLength);
                    }

                    std::array<uint8_t, 32> uploadedHash{};
                    if(mbedtls_sha256(ecuFlashStagingBuffer.data(), block.length,
                                    uploadedHash.data(), 0) != 0)
                    {
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SHA-256 failed");
                        return ESP_OK;
                    }

                    uploaded += uploadedBlockLength;
                    if(uploadedHash == block.sha256)
                    {
                        ESP_LOGI("ECU", "Block %u at 0x%08lX unchanged (%llu/%llu bytes)",
                                block.id,
                                static_cast<unsigned long>(block.address),
                                static_cast<unsigned long long>(uploaded),
                                static_cast<unsigned long long>(lengthValue));
                        continue;
                    }
                }

                ESP_LOGI("ECU", "Queueing erase for changed block %u at 0x%08lX", block.id,
                         static_cast<unsigned long>(block.address));
                UDSResponse eraseResponse = udsService->QueueEraseFlashBlock(block.id);
                if(!eraseResponse.Ok())
                {
                    ESP_LOGE("UDS", "Erase block %u failed (status %u, NRC 0x%02X)",
                             block.id,
                             static_cast<unsigned>(eraseResponse.status),
                             eraseResponse.negativeResponseCode);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash erase failed");
                    return ESP_OK;
                }

                UDSResponse downloadResponse =
                    udsService->RequestDownload(block.address, block.length);
                if(!downloadResponse.Ok())
                {
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "UDS RequestDownload failed");
                    return ESP_OK;
                }

                const bool compressed = udsService->UsesLZ4Compression();
                const size_t maximumMessageLength = udsService->MaximumBlockLength();
                const size_t overhead = compressed ? 4U : 2U;
                if(maximumMessageLength <= overhead)
                {
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid download block length");
                    return ESP_OK;
                }

				size_t blockOffset = 0;
				uint8_t sequenceCounter = 1;
				while(blockOffset < block.length)
				{
                    const size_t compressedCapacity = maximumMessageLength - overhead;
                    size_t transferLength = compressed
                        ? std::min<size_t>(ecuTransferBuffer.size(),
                                           block.length - blockOffset)
                        : std::min({compressedCapacity,
                                    ecuTransferBuffer.size(),
                                    static_cast<size_t>(block.length - blockOffset)});

                    // A raw LZ4 block can grow slightly for incompressible data.
                    // Choose a size whose standard compress-bound fits the UDS
                    // payload so a streamed HTTP chunk never has to be split after
                    // it has already been consumed from the socket.
                    if(compressed)
                    {
                        while(transferLength != 0U &&
                              transferLength + transferLength / 255U + 16U >
                                  compressedCapacity)
                            transferLength -= 8U;
                    }
                    transferLength &= ~size_t{7};
                    if(transferLength == 0)
                    {
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unaligned flash transfer");
                        return ESP_OK;
                    }

                    // Read or copy this sequence exactly once. Queue-busy and
                    // timeout retries below must resend these same bytes; reading
                    // from the HTTP socket again would silently discard firmware.
                    if(blockBuffered)
                    {
                        std::copy(
                            ecuFlashStagingBuffer.data() + blockOffset,
                            ecuFlashStagingBuffer.data() + blockOffset + transferLength,
                            ecuTransferBuffer.begin());
                    }
                    else
                    {
                        const size_t bytesFromUpload = blockOffset < uploadedBlockLength
                            ? std::min(transferLength, uploadedBlockLength - blockOffset)
                            : 0U;
                        size_t received = 0;
                        while(received < bytesFromUpload)
                        {
                            const int receiveLength = httpd_req_recv(
                                req,
                                reinterpret_cast<char *>(ecuTransferBuffer.data() + received),
                                bytesFromUpload - received);
                            if(receiveLength <= 0)
                            {
                                if(receiveLength == HTTPD_SOCK_ERR_TIMEOUT)
                                    httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Socket timeout");
                                else
                                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete upload");
                                return ESP_OK;
                            }
                            received += static_cast<size_t>(receiveLength);
                        }
                        std::fill(
                            ecuTransferBuffer.begin() + bytesFromUpload,
                            ecuTransferBuffer.begin() + transferLength,
                            0xFFU);
                    }

					UDSResponse transferResponse;
					unsigned timeoutRetries = 0;
					while(true)
					{
                        transferResponse =
                            udsService->TransferData(sequenceCounter,
                                                     ecuTransferBuffer.data(),
                                                     transferLength);
						if(transferResponse.status == UDSStatus::NegativeResponse &&
						   transferResponse.negativeResponseCode == 0x21)
						{
							vTaskDelay(pdMS_TO_TICKS(1));
							continue;
						}
						// A queued write and its acknowledgment are idempotent for a
						// repeated block sequence counter. Retry a dropped response,
						// but remain bounded if the ECU has actually stopped running.
						if(transferResponse.status == UDSStatus::Timeout &&
						   timeoutRetries < 3U)
						{
							++timeoutRetries;
							vTaskDelay(pdMS_TO_TICKS(5));
							continue;
						}
						break;
					}
                    if(!transferResponse.Ok() || transferResponse.payload.empty() ||
                       transferResponse.payload[0] != sequenceCounter)
                    {
                        ESP_LOGE("UDS", "TransferData 0x%02X failed (status %u, NRC 0x%02X)",
                                 sequenceCounter,
                                 static_cast<unsigned>(transferResponse.status),
                                 transferResponse.negativeResponseCode);
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "UDS TransferData failed");
                        return ESP_OK;
                    }
                    blockOffset += transferLength;
                    ++sequenceCounter;
                }

                UDSResponse exitResponse;
                do
                {
                    exitResponse = udsService->RequestTransferExit();
                    if(exitResponse.status == UDSStatus::NegativeResponse &&
                       exitResponse.negativeResponseCode == 0x21)
                        vTaskDelay(pdMS_TO_TICKS(1));
                    else
                        break;
                } while(true);
                if(!exitResponse.Ok())
                {
                    ESP_LOGE("UDS", "Block %u transfer exit failed (status %u, NRC 0x%02X)",
                             block.id,
                             static_cast<unsigned>(exitResponse.status),
                             exitResponse.negativeResponseCode);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash programming failed");
                    return ESP_OK;
                }

                if(!blockBuffered)
                    uploaded += uploadedBlockLength;

                ESP_LOGI("ECU", "Programmed block %u at 0x%08lX (%llu/%llu bytes)",
                         block.id,
                         static_cast<unsigned long>(block.address),
                         static_cast<unsigned long long>(uploaded),
                         static_cast<unsigned long long>(lengthValue));
            }

            httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
    };
    httpd_register_uri_handler(server, &uploadECUPost);

    const httpd_uri_t downloadECUPost = {
        .uri       = "/command/downloadECU",
        .method     = HTTP_POST,
        .handler    = [udsService](httpd_req_t *req)
        {
            constexpr uint64_t maximumUploadBlockLength = 8192;
            char query[128];
            char addressText[32];
            char lengthText[32];

            if(httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
               httpd_query_key_value(query, "address", addressText, sizeof(addressText)) != ESP_OK ||
               httpd_query_key_value(query, "length", lengthText, sizeof(lengthText)) != ESP_OK)
            {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "address and length query parameters are required");
                return ESP_OK;
            }

            auto parseNumber = [](const char *text, uint64_t *value) {
                if(text == nullptr || value == nullptr || text[0] == '\0')
                    return false;
                errno = 0;
                char *end = nullptr;
                const unsigned long long parsed = strtoull(text, &end, 0);
                if(errno != 0 || end == text || *end != '\0')
                    return false;
                *value = parsed;
                return true;
            };

            uint64_t addressValue = 0;
            uint64_t lengthValue = 0;
            if(!parseNumber(addressText, &addressValue) ||
               !parseNumber(lengthText, &lengthValue) ||
               addressValue > std::numeric_limits<uint32_t>::max() ||
               lengthValue == 0 ||
               lengthValue > std::numeric_limits<uint32_t>::max() ||
               addressValue + lengthValue > (uint64_t{1} << 32))
            {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid address or length");
                return ESP_OK;
            }

            UDSResponse uploadResponse = udsService->RequestUpload(addressValue, lengthValue);
            if(!uploadResponse.Ok())
            {
                ESP_LOGE("UDS", "RequestUpload failed (status %u, NRC 0x%02X)",
                         static_cast<unsigned>(uploadResponse.status),
                         uploadResponse.negativeResponseCode);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "UDS RequestUpload failed");
                return ESP_OK;
            }

            if(uploadResponse.payload.empty())
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid UDS RequestUpload response");
                return ESP_OK;
            }

            const uint8_t blockLengthBytes = uploadResponse.payload[0] >> 4;
            if(blockLengthBytes == 0 ||
               blockLengthBytes > 8 ||
               uploadResponse.payload.size() < static_cast<size_t>(blockLengthBytes) + 1)
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid UDS upload block length");
                return ESP_OK;
            }

            uint64_t maximumBlockLength = 0;
            for(uint8_t i = 0; i < blockLengthBytes; ++i)
                maximumBlockLength = (maximumBlockLength << 8) | uploadResponse.payload[i + 1];
            if(maximumBlockLength <= 2 || maximumBlockLength > maximumUploadBlockLength)
            {
                ESP_LOGE("UDS", "Unsupported upload block length: %llu", static_cast<unsigned long long>(maximumBlockLength));
                udsService->RequestTransferExit();
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unsupported UDS upload block length");
                return ESP_OK;
            }
            const size_t maximumDataLength = static_cast<size_t>(maximumBlockLength - 2);
            const bool compressedUpload = udsService->UsesLZ4Compression();

            httpd_resp_set_type(req, "application/octet-stream");
            httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=ecu.bin");

            uint32_t remaining = static_cast<uint32_t>(lengthValue);
            uint32_t downloaded = 0;
            uint8_t blockSequenceCounter = 1;
            bool responseStarted = false;
            while(remaining > 0)
            {
                UDSResponse transferResponse = udsService->TransferData(blockSequenceCounter);
                if(!transferResponse.Ok())
                {
                    ESP_LOGE("UDS", "TransferData block %u failed (status %u, NRC 0x%02X)",
                             blockSequenceCounter,
                             static_cast<unsigned>(transferResponse.status),
                             transferResponse.negativeResponseCode);
                    udsService->RequestTransferExit();
                    if(!responseStarted)
                    {
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "UDS TransferData failed");
                        return ESP_OK;
                    }
                    return ESP_FAIL; // Close an already-started response so the client detects a truncated dump.
                }

                if(transferResponse.payload.size() < 2 || transferResponse.payload[0] != blockSequenceCounter)
                {
                    ESP_LOGE("UDS", "Invalid TransferData response for block %u", blockSequenceCounter);
                    udsService->RequestTransferExit();
                    if(!responseStarted)
                    {
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid UDS TransferData response");
                        return ESP_OK;
                    }
                    return ESP_FAIL;
                }

                const size_t chunkLength = transferResponse.payload.size() - 1;
                const size_t maximumDecodedLength = compressedUpload ? 4096 : maximumDataLength;
                if(chunkLength > maximumDecodedLength || chunkLength > remaining)
                {
                    ESP_LOGE("UDS", "Invalid TransferData payload length: %u", static_cast<unsigned>(chunkLength));
                    udsService->RequestTransferExit();
                    if(!responseStarted)
                    {
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid UDS TransferData length");
                        return ESP_OK;
                    }
                    return ESP_FAIL;
                }

                if(httpd_resp_send_chunk(req,
                                         reinterpret_cast<const char *>(transferResponse.payload.data() + 1),
                                         chunkLength) != ESP_OK)
                {
                    ESP_LOGE("ECU", "Failed to stream ECU data to HTTP client");
                    udsService->RequestTransferExit();
                    return ESP_FAIL;
                }

                responseStarted = true;
                remaining -= static_cast<uint32_t>(chunkLength);
                downloaded += static_cast<uint32_t>(chunkLength);
                ++blockSequenceCounter;
                if(remaining == 0 || downloaded % (64 * 1024) < chunkLength)
                {
                    ESP_LOGI("ECU", "Downloaded %lu/%llu ECU bytes",
                             static_cast<unsigned long>(downloaded),
                             static_cast<unsigned long long>(lengthValue));
                }
            }

            UDSResponse exitResponse = udsService->RequestTransferExit();
            if(!exitResponse.Ok())
            {
                ESP_LOGE("UDS", "RequestTransferExit failed (status %u, NRC 0x%02X)",
                         static_cast<unsigned>(exitResponse.status),
                         exitResponse.negativeResponseCode);
            }
            if(httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK)
                return ESP_FAIL;
            return ESP_OK;
        }
    };
    httpd_register_uri_handler(server, &downloadECUPost);

    //do this last since it takes up all endpoints
    mount_mmrofs("/mm");
    register_file_handler_http_server("/mm");

    while(true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
