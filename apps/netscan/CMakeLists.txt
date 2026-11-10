idf_component_register(SRCS "netscan.c" "lan.c" "netscan_util.c"
                       INCLUDE_DIRS "include"
                       REQUIRES appfw
                       PRIV_REQUIRES ui led net clock config storage esp_netif lwip esp_timer)
