idf_component_register(
    SRCS "main.c" "ledcontrol.c" "wifi.c" "graph_client.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_client
                  esp-tls esp_driver_gpio uri_encode
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wstrict-prototypes -Werror=implicit-function-declaration)
