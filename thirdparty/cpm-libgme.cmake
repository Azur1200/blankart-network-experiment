CPMAddPackage(
	NAME libgme
	VERSION 0.6.5
	URL "https://github.com/libgme/game-music-emu/archive/refs/tags/0.6.5.zip"
	EXCLUDE_FROM_ALL ON
	OPTIONS
		"BUILD_SHARED_LIBS ${SRB2_CONFIG_SHARED_INTERNAL_LIBRARIES}"
		"ENABLE_UBSAN OFF"
		"GME_YM2612_EMU MAME"
)
