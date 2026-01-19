CPMAddPackage(
	NAME openal-soft
	VERSION 1.25.0
	URL "https://github.com/kcat/openal-soft/archive/refs/tags/1.25.0.zip"
	EXCLUDE_FROM_ALL ON
	OPTIONS
		"AL_LIBTYPE_STATIC ON"
		"ALSOFT_BACKEND_SDL3 OFF"
		"ALSOFT_BACKEND_SDL2 ON"
		"ALSOFT_REQUIRE_SDL2 ON"
		"ALSOFT_REQUIRE_SDL3 OFF"
		"ALSOFT_BACKEND_PIPEWIRE OFF"
		"ALSOFT_UTILS OFF"
)

if(openal-soft)
	target_compile_features(OpenAL PRIVATE cxx_std_11)
endif()
