# Host-side tests for the parts of the firmware that don't touch hardware.
#   cmake -S test/host -B build/host && cmake --build build/host && ctest --test-dir build/host
cmake_minimum_required(VERSION 3.16)
project(fanwave_host_tests C)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror -fsanitize=address,undefined)
add_link_options(-fsanitize=address,undefined)

set(COMPONENTS ${CMAKE_CURRENT_LIST_DIR}/../../components)

add_executable(test_protocol test_protocol.c ${COMPONENTS}/protocol/fw_protocol.c)
target_include_directories(test_protocol PRIVATE ${COMPONENTS}/protocol/include)
target_link_libraries(test_protocol m)

enable_testing()
add_test(NAME protocol COMMAND test_protocol)

add_executable(test_status_pattern test_status_pattern.c ${COMPONENTS}/status_led/status_pattern.c)
target_include_directories(test_status_pattern PRIVATE ${COMPONENTS}/status_led/include)
target_link_libraries(test_status_pattern m)
add_test(NAME status_pattern COMMAND test_status_pattern)

add_executable(test_scan_math test_scan_math.c ${COMPONENTS}/rf_scan/scan_math.c)
target_include_directories(test_scan_math PRIVATE ${COMPONENTS}/rf_scan/include)
add_test(NAME scan_math COMMAND test_scan_math)

add_executable(test_rf_segment test_rf_segment.c ${COMPONENTS}/rf/rf_segment.c)
target_include_directories(test_rf_segment PRIVATE ${COMPONENTS}/rf/include)
add_test(NAME rf_segment COMMAND test_rf_segment)

add_executable(test_rx_pipeline test_rx_pipeline.c ${COMPONENTS}/rf/rf_segment.c ${COMPONENTS}/protocol/fw_protocol.c)
target_include_directories(test_rx_pipeline PRIVATE ${COMPONENTS}/rf/include ${COMPONENTS}/protocol/include)
target_link_libraries(test_rx_pipeline m)
add_test(NAME rx_pipeline COMMAND test_rx_pipeline)
