#include "esp_err.h"
#include "esp_http_server.h"

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

extern httpd_handle_t server;
esp_err_t start_http_server();
esp_err_t register_file_handler_http_server(const char *base_path);

#endif