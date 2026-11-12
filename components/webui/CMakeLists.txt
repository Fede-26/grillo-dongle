idf_component_register(SRCS "webui.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx net
                       PRIV_REQUIRES ui qr config esp_hw_support)
