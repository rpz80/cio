include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)

externalproject_add(
    ct

    GIT_REPOSITORY "https://github.com/rpz80/ct.git"
    GIT_TAG "master"

    SOURCE_DIR "${CMAKE_SOURCE_DIR}/test/3rdparty/ct"
    CMAKE_ARGS -DwithExample=OFF -DCMAKE_INSTALL_PREFIX=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/ct
    MAKE_COMMNAD ""
    BUILD_BYPRODUCTS "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/ct/lib/libct.a"
)

include_directories("${CMAKE_SOURCE_DIR}/cio/src" "${CMAKE_SOURCE_DIR}/test/3rdparty/ct/src")

file(GLOB TEST_SRC "src/*.c" "src/*.h")
add_executable(cio_test ${TEST_SRC})
find_package(Threads)
target_link_libraries(
    cio_test PRIVATE
    cio "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/ct/lib/libct.a" ${CMAKE_THREAD_LIBS_INIT})
add_dependencies(cio_test ct)
