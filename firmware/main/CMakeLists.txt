idf_component_register(SRCS "main.c" "console.c"
                       INCLUDE_DIRS "."
                       REQUIRES cc1101 rf protocol status_led console nvs_flash esp_driver_uart)
