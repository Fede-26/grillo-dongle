idf_component_register(SRCS "appmgr.c" "app_ui.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx usbdev freertos input
                       PRIV_REQUIRES ui led storage syslog net)
