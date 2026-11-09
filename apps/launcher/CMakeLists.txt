idf_component_register(SRCS "launcher.c"
                       INCLUDE_DIRS "include"
                       REQUIRES appfw
                       PRIV_REQUIRES ui led config esp_timer)
