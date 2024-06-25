add_library(system_config "")

target_sources(system_config PRIVATE "${CMAKE_CURRENT_LIST_DIR}/system_config.c")

target_include_directories(system_config PUBLIC "${PROJ_INCLUDE_DIR}")