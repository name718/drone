idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer freertos log esp_psram esp_system spi_flash
)
