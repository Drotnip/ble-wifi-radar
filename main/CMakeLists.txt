idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt esp_adc esp_driver_gpio esp_driver_spi esp_lcd
                                fatfs sdmmc esp_driver_sdspi esp_wifi esp_netif
                                esp_event nvs_flash esp_driver_uart esp_timer
                                esp_driver_ledc heap)
