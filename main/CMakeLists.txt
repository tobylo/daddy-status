idf_component_register(
    SRCS "main.c" "ledcontrol.c" "wifi.c" "graph_client.c" "protocol.c"
         "auth_client.c" "http_transport.c" "token_storage.c" "presence.c" "app_state.c" "led_frame.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_client
                  esp-tls esp_driver_gpio esp_timer
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wstrict-prototypes -Werror=implicit-function-declaration)
