idf_component_register(SRCS "injector.c"
                       INCLUDE_DIRS "include"
                       REQUIRES appfw
                       PRIV_REQUIRES injector_core ui storage usbdev config)
