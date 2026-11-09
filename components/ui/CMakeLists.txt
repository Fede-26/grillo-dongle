idf_component_register(SRCS "ui.c" "icons.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx
                       PRIV_REQUIRES display esp_timer)
