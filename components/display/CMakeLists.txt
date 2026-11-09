idf_component_register(SRCS "display.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board esp_lcd
                       PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_spi)
