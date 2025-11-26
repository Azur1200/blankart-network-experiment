CPMAddPackage(
	NAME ogg
	VERSION 1.3.5
	URL "https://github.com/xiph/ogg/releases/download/v1.3.5/libogg-1.3.5.zip"
	EXCLUDE_FROM_ALL ON
)

if(ogg_ADDED)
	# Fixes bug with find_package not being able to find
	# ogg when cross-building.
	set(OGG_INCLUDE_DIR "${ogg_SOURCE_DIR}/include")
	set(OGG_LIBRARY Ogg:ogg)
endif()

CPMAddPackage(
	NAME vorbis
	VERSION 1.3.7
	URL "https://github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.zip"
	EXCLUDE_FROM_ALL ON
)

if(vorbis_ADDED)
	add_library(Vorbis::vorbis ALIAS vorbis)
	add_library(Vorbis::vorbisenc ALIAS vorbisenc)
endif()

CPMAddPackage(
	NAME opus
	VERSION 1.5.2
	URL "https://github.com/xiph/opus/archive/refs/tags/v1.5.2.zip"
	EXCLUDE_FROM_ALL ON
)

if(opus_ADDED)
	add_library(Opus::opus ALIAS opus)
endif()

CPMAddPackage(
	NAME libsndfile-fork
	VERSION 1.2.3
	URL "https://codeberg.org/NepDisk/libsndfile-fork/archive/1.2.3.zip"
	EXCLUDE_FROM_ALL ON
)
