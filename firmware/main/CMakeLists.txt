idf_component_register(SRCS "main.c" "console.c" "api.c" "captures.c" "fans.c" "settings.c" "storage.c" "wifi.c"
                       INCLUDE_DIRS "."
                       REQUIRES cc1101 rf protocol status_led console nvs_flash esp_driver_uart
                                esp_wifi esp_netif esp_event esp_http_server app_update spiffs json)

# Put the web app in the "www" partition when it has been built (cd web && npm run build).
set(WEB_DIST "${CMAKE_CURRENT_SOURCE_DIR}/../../web/dist")
if(EXISTS "${WEB_DIST}/index.html")
    spiffs_create_partition_image(www "${WEB_DIST}" FLASH_IN_PROJECT)
else()
    message(WARNING "web/dist not found: the firmware will run without the web app. Run `npm run build` in web/ first.")
endif()
