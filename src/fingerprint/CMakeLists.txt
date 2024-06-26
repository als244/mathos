add_library(fingerprint "")

target_sources(fingerprint PRIVATE "${CMAKE_CURRENT_LIST_DIR}/fingerprint.c")

target_include_directories(fingerprint PUBLIC "${PROJ_INCLUDE_DIR}")

find_library(LIB_CRYPTO crypto)

target_link_libraries(fingerprint PRIVATE "${LIB_CRYPTO}")