idf_component_register(SRCS "devcon.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES usbdev esp_driver_usb_serial_jtag log)
