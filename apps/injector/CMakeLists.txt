idf_component_register(SRCS "injector.c" "engine_lua.c" "payload.c"
                       INCLUDE_DIRS "include"
                       REQUIRES appfw
                       PRIV_REQUIRES ui led storage usbdev hid luart clock config esp_timer devcon)
