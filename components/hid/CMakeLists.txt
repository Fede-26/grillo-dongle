idf_component_register(SRCS "hid.c" "hid_layout.c" "layouts_data.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES usbdev esp_timer esp_hw_support)
