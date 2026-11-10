idf_component_register(SRCS "websrv.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server
                       PRIV_REQUIRES led esp_timer lwip)
