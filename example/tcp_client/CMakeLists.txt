add_executable(tcp_client "main.c")
find_package(Threads)
target_link_libraries(tcp_client cio ${CMAKE_THREAD_LIBS_INIT})
add_dependencies(tcp_client cio)
