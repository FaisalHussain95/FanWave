# Plain C with no ESP-IDF dependencies, so test/host builds it too.
idf_component_register(SRCS "scan_math.c" INCLUDE_DIRS "include")
