# Plain C, no IDF dependencies: also built on the host by test/host.
idf_component_register(SRCS "fw_protocol.c"
                       INCLUDE_DIRS "include")
