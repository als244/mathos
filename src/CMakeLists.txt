add_executable(example_client main.c)

add_subdirectory(generic_structures)
add_subdirectory(utils)
add_subdirectory(system_config)
add_subdirectory(system_init)
add_subdirectory(request_handler)
add_subdirectory(fingerprinting)
add_subdirectory(caching)
add_subdirectory(communication)
add_subdirectory(backend_init)
add_subdirectory(memory)
add_subdirectory(compute)
add_subdirectory(backends)
add_subdirectory(sched)
add_subdirectory(client)

target_link_libraries(example_client PRIVATE generic_structures utils system_config system_init request_handler fingerprinting caching communication backend_init memory compute sched
									 INTERFACE backends
									 PUBLIC client)

