CPMAddPackage(
	NAME openal-soft
	VERSION 1.25.1
	URL "https://github.com/kcat/openal-soft/archive/refs/tags/1.25.1.zip"
	EXCLUDE_FROM_ALL ON
	OPTIONS
		"ALSOFT_BACKEND_SDL3 ON"
		"ALSOFT_BACKEND_SDL2 OFF"
		"ALSOFT_REQUIRE_SDL2 OFF"
		"ALSOFT_REQUIRE_SDL3 ON"
		"ALSOFT_BACKEND_PIPEWIRE OFF"
		"ALSOFT_UTILS OFF"
)

if(openal-soft)
	target_compile_features(OpenAL PRIVATE cxx_std_11)
endif()
