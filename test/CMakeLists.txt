include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)

externalproject_add(
    ct

    GIT_REPOSITORY "https://github.com/rpz80/ct.git"
    GIT_TAG "master"

    SOURCE_DIR "${CMAKE_SOURCE_DIR}/test/3rdparty/ct"
    CMAKE_ARGS -DwithExample=OFF -DCMAKE_INSTALL_PREFIX=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/ct
    MAKE_COMMNAD ""
)

include_directories("${CMAKE_SOURCE_DIR}/cio/src" "${CMAKE_SOURCE_DIR}/test/3rdparty/ct/src")

file(GLOB TEST_SRC "src/*.c" "src/*.h")
add_executable(test ${TEST_SRC})
target_link_libraries(test PRIVATE cio "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/ct/lib/libct.a")
add_dependencies(test ct)
