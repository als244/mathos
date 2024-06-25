add_library(utils "")

target_sources(utils PUBLIC "${CMAKE_CURRENT_LIST_DIR}/utils.c")

target_include_directories(utils PUBLIC "${PROJ_INCLUDE_DIR}")