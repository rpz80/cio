add_executable(tcp_server ../common.h ../common.c main.c)
find_package(Threads)
target_link_libraries(tcp_server cio ${CMAKE_THREAD_LIBS_INIT})
add_dependencies(tcp_server cio)
