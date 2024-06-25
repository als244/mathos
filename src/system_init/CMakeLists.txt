add_library(system_init "")

target_sources(system_init PRIVATE "${CMAKE_CURRENT_LIST_DIR}/system_init.c" "${PROJ_SRC_DIR}/system_config.c")

target_include_directories(system_init PUBLIC "${PROJ_INCLUDE_DIR}")