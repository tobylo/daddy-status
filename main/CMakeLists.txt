idf_component_register(
    SRCS "storage.c" "firmware_update.c" "settings.c" "main.c" "web_server.c" "ledcontrol.c" "wifi.c" "graph_client.c" "protocol.c"
         "auth_client.c" "http_transport.c" "token_storage.c" "presence.c" "app_state.c" "led_frame.c" "diagnostics.c"
    EMBED_TXTFILES "auth.html"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_client
                  bootloader_support app_update esp-tls esp_driver_gpio esp_timer
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wstrict-prototypes -Werror=implicit-function-declaration)
# Limit parser recursion to fit the ESP32 worker stack. No payload needs deep nesting.
idf_component_get_property(cjson_lib espressif__cjson COMPONENT_LIB)
target_compile_definitions(${cjson_lib} PRIVATE CJSON_NESTING_LIMIT=16)
