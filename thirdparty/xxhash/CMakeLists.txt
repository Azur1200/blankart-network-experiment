# Update from https://github.com/Cyan4973/xxHash/releases/
# xxHash version 0.8.3
# License: BSD 2-clause

add_library(xxhash STATIC "${CMAKE_CURRENT_SOURCE_DIR}/include/xxhash.c")
target_compile_options(xxhash PRIVATE -O3) # do some optimization will ya
target_include_directories(xxhash PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_library(xxHash::xxhash ALIAS xxhash)
