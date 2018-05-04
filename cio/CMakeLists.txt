file(GLOB CIO_SRC "src/*.h" "src/*.c")
add_library(cio "${CIO_SRC}")
include_directories(cio "include")