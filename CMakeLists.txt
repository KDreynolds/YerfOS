cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Add this line to specify the custom partition table
set(PARTITION_TABLE ${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv)

project(4SkinOS)
