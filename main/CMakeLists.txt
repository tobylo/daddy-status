idf_component_register(
    SRCS "main.c" "ledcontrol.c" "wifi.c" "esp_http_client_example.c"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES howsmyssl_com_root_cert.pem
)