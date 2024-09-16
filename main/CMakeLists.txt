idf_component_register(SRCS "kernel.c" "filesystem.c"
                       INCLUDE_DIRS "." "include"
                       REQUIRES console esp_system esp_driver_uart esp_partition)