idf_component_register(SRCS "input.c" "gesture.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board esp_driver_gpio esp_timer)
