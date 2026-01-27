# Main component CMakeLists.txt

# Check if certificate exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/certs/github_ca.pem")
    set(EMBED_CERT "certs/github_ca.pem")
else()
    set(EMBED_CERT "")
endif()

idf_component_register(
    SRCS 
        "main.c"
        "ble_service.c"
        "wifi_manager.c"
        "mqtt_handler.c"
        "uart_handler.c"
        "data_parser.c"
        "nvs_storage.c"
        "crc_utils.c"
        "cmd_handler.c"
        "ota_handler.c"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        bt
        mqtt
        json
        driver
        esp_https_ota
        esp_http_client
        app_update
    EMBED_TXTFILES
        ${EMBED_CERT}
)
