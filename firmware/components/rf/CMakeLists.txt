# rf_segment.c is plain C and also built on the host by test/host.
idf_component_register(SRCS "rf.c" "rf_segment.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_rmt esp_timer cc1101)
