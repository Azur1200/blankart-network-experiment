CPMAddPackage(
	NAME ZLIB-NG
	VERSION 2.2.4
	URL "https://github.com/zlib-ng/zlib-ng/archive/refs/tags/2.2.4.zip"
	EXCLUDE_FROM_ALL
	OPTIONS
		"ZLIB_COMPAT ON"
		"WITH_GTEST OFF"
		"BUILD_SHARED_LIBS ${SRB2_CONFIG_SHARED_INTERNAL_LIBRARIES}"
)

if(ZLIB-NG_ADDED)
    add_library(ZLIB::ZLIB ALIAS zlib)
endif()
