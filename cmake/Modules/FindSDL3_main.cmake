# Find SDL3
# Once done, this will define
#
#  SDL3_MAIN_FOUND - system has SDL3
#  SDL3_MAIN_INCLUDE_DIRS - SDL3 include directories
#  SDL3_MAIN_LIBRARIES - link libraries

include(LibFindMacros)

libfind_pkg_check_modules(SDL3_MAIN_PKGCONF SDL3)

# includes
find_path(SDL3_MAIN_INCLUDE_DIR
	NAMES SDL.h
	PATHS
		${SDL3_MAIN_PKGCONF_INCLUDE_DIRS}
		"/usr/include/SDL3"
		"/usr/local/include/SDL3"
)

# library
find_library(SDL3_MAIN_LIBRARY
	NAMES SDL3_main
	PATHS
		${SDL3_MAIN_PKGCONF_LIBRARY_DIRS}
		"/usr/lib"
		"/usr/local/lib"
)


# set include dir variables
set(SDL3_MAIN_PROCESS_INCLUDES SDL3_MAIN_INCLUDE_DIR)
set(SDL3_MAIN_PROCESS_LIBS SDL3_MAIN_LIBRARY)
libfind_process(SDL3_MAIN)
