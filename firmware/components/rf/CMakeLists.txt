idf_component_register(SRCS "rf.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_rmt esp_timer cc1101)
