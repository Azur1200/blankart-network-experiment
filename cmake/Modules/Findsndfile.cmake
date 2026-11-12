include(LibFindMacros)

libfind_pkg_check_modules(SNDFILE_PKGCONF SNDFILE)

find_path(SNDFILE_INCLUDE_DIR
	NAMES sndfile.h
	PATHS
		${SNDFILE_PKGCONF_INCLUDE_DIRS}
		"/usr/include/"
		"/usr/local/include/"
)

find_library(SNDFILE_LIBRARY
	NAMES sndfile
	PATHS
		${SNDFILE_PKGCONF_LIBRARY_DIRS}
		"/usr/lib"
		"/usr/local/lib"
)

set(SNDFILE_PROCESS_INCLUDES SNDFILE_INCLUDE_DIR)
set(SNDFILE_PROCESS_LIBS SNDFILE_LIBRARY)
libfind_process(SNDFILE)

if(SNDFILE_FOUND AND NOT TARGET sndfile)
	add_library(sndfile UNKNOWN IMPORTED)
	set_target_properties(
		sndfile
		PROPERTIES
		IMPORTED_LOCATION "${SNDFILE_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${SNDFILE_INCLUDE_DIR}"
	)
endif()
