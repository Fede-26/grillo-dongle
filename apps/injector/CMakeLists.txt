idf_component_register(SRCS "injector.c" "engine_lua.c" "payload.c"
                       INCLUDE_DIRS "include"
                       REQUIRES appfw
                       PRIV_REQUIRES ui led storage syslog usbdev hid luart config esp_timer)
