idf_component_register(SRCS "main.c" "app_list.c" "console_cmds.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES display ui led storage syslog config appfw launcher usbdisk esp_app_format devcon mbedtls)
