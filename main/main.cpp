#include <stdio.h>
#include <string.h>
#include <algorithm>
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
#include "mount.h"
#include "ECUService_E92.h"
#include "ECUService_E38.h"

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

    _ecuWebSocketService = new Esp32IdfCommunicationService_WebSocket(server, "/ECUWebSocket");

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
        _ecuWebSocketService->Send(data, length);
        esp_log_buffer_hex("ISOTP", data, std::min(length, (size_t)20));
        return 0;
    });
    _ecuWebSocketService->RegisterReceiveCallBack([_ecuISOTPService](communication_send_callback_t send, const void *data, size_t length) {
        _ecuISOTPService->Send(data, length);
        esp_log_buffer_hex("WebSocket", data, std::min(length, (size_t)20));
        return length;
    });

    static IECUService *ecuService = new ECUService_E92(_ecuISOTPService, [](uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); });

    const httpd_uri_t uploadKernelPost = {
        .uri       = "/command/uploadKernel",
        .method     = HTTP_POST,
        .handler    = [ecuService](httpd_req_t *req) 
        {
            int total_len = req->content_len;
            int cur_len = 0;
            
            // Allocate buffer for kernel data
            uint8_t *kernel_data = (uint8_t *)malloc(total_len);
            if (!kernel_data) {
                ESP_LOGE("ECU", "Failed to allocate memory for kernel data");
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
                return ESP_OK;
            }
            
            // Read the POST body
            while (cur_len < total_len) {
                int recv_len = httpd_req_recv(req, (char *)kernel_data + cur_len, total_len - cur_len);
                if (recv_len <= 0) {
                    if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                        ESP_LOGE("ECU", "Socket timeout while reading kernel data");
                        httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Socket timeout");
                        free(kernel_data);
                        return ESP_OK;
                    }
                    break;
                }
                cur_len += recv_len;
            }
            
            if (cur_len != total_len) {
                ESP_LOGE("ECU", "Failed to read all kernel data: received %d/%d bytes", cur_len, total_len);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete kernel data");
                free(kernel_data);
                return ESP_OK;
            }

            ESP_LOGI("ECU", "Received kernel data (%d bytes)", total_len);

            if(ecuService->UnlockECU() == ECU_SERVICE_ERROR_NONE) {
                ESP_LOGI("ECU", "ECU unlocked successfully");
            } else {
                ESP_LOGE("ECU", "Failed to unlock ECU");
                free(kernel_data);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to unlock ECU");
                return ESP_OK;
            }

            if(ecuService->SuppressBroadcastMessages(true) == ECU_SERVICE_ERROR_NONE) {
                ESP_LOGI("ECU", "Broadcast messages suppressed successfully");
            } else {
                ESP_LOGE("ECU", "Failed to suppress broadcast messages");
                free(kernel_data);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to suppress broadcast messages");
                return ESP_OK;
            }

            if(ecuService->EnableProgrammingMode() != ECU_SERVICE_ERROR_NONE) {
                ESP_LOGE("ECU", "Failed to enable programming mode");
                free(kernel_data);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to enable programming mode");
                return ESP_OK;
            }

            if(ecuService->WriteMemory(0x40010000, kernel_data, total_len) != ECU_SERVICE_ERROR_NONE) {
                ESP_LOGE("ECU", "Failed to write kernel to memory");
                free(kernel_data);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write kernel to memory");
                return ESP_OK;
            }

            if(ecuService->ExecuteMemory(0x40010000) == ECU_SERVICE_ERROR_NONE) {
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

    //do this last since it takes up all endpoints
    mount_mmrofs("/mm");
    register_file_handler_http_server("/mm");

    while(true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
