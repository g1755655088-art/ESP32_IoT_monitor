idf_component_register(
    SRCS "ESP32_IoT_monitor.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio
)