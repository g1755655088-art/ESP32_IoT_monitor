idf_component_register(
    SRCS "wifi_app.c" "MQTT_app.c" "ESP32_IoT_monitor.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_i2c
        esp_driver_gpio
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
)
