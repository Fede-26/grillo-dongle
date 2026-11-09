idf_component_register(SRCS "usbdisk.c"
                       INCLUDE_DIRS "include"
                       REQUIRES appfw
                       PRIV_REQUIRES ui led storage usbdev esp_timer)
