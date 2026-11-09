idf_component_register(SRCS "usbdev.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sdmmc
                       PRIV_REQUIRES espressif__esp_tinyusb esp_hw_support esp_hal_usb fatfs)
