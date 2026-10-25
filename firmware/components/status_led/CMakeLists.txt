# status_pattern.c is plain C and also built on the host by test/host.
idf_component_register(SRCS "status_led.c" "status_pattern.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_rmt esp_timer)
