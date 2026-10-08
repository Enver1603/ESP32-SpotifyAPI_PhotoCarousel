idf_component_register(
    SRCS "softap_example_main.c"
    "spotify_font_tr.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_server
        esp_http_client
        mbedtls
        esp_driver_gpio
        esp_lcd
        fatfs
		sdmmc
)