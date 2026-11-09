idf_component_register(SRCS "storage.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sdmmc
                       PRIV_REQUIRES board fatfs esp_driver_sdmmc)
