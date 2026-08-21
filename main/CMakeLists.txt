idf_component_register(SRCS "main.cpp" "http_server.cpp" "mount.c" "IECUService.cpp" "ECUService_E92.cpp" "ECUService_GMCAN.cpp" "ECUService_E38.cpp" "ECUService_E78.cpp" "UDS.cpp" "LZ4.cpp"
                    INCLUDE_DIRS "."
                    EMBED_FILES "Kernel-E38.bin" "Kernel-E92.bin"
                    PRIV_REQUIRES esp_EmbeddedIOServices esp_driver_gpio esp_driver_gptimer esp_adc esp_driver_spi esp_driver_uart esp_ringbuf esp_http_server fatfs esp_wifi nvs_flash driver app_update spiffs MMROFS spi_flash)
