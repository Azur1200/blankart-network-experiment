add_executable(BLANKART MACOSX_BUNDLE WIN32
	comptime.c
	config.h.in
	cxxutil.hpp
	qs22j.c
	string.c
	strbuf.c
	d_main.cpp
	d_clisrv.c
	d_net.c
	d_netfil.c
	d_netcmd.c
	dehacked.c
	deh_soc.c
	deh_lua.c
	deh_tables.c
	z_zone.cpp
	f_finale.c
	f_dscredits.cpp
	f_wipe.c
	g_demo.c
	g_game.c
	g_input.c
	g_party.cpp
	am_map.c
	command.c
	console.c
	font.c
	hu_stuff.c
	i_time.c
	y_inter.c
	st_stuff.c
	m_aatree.c
	m_anigif.c
	m_argv.c
	m_bbox.c
	m_cheat.c
	m_cond.c
	m_easing.c
	m_fixed.c
	m_menu.c
	m_textinput.c
	m_misc.cpp
	m_emotes.cpp
	m_perfstats.c
	m_random.c
	m_queue.c
	info.c
	p_ceilng.c
	p_enemy.c
	p_floor.c
	p_inter.c
	p_lights.c
	p_map.c
	p_maputl.c
	p_mapthing.cpp
	p_mobj.c
	p_polyobj.c
	p_saveg.c
	p_setup.c
	p_sight.c
	p_spec.c
	p_telept.c
	p_tick.c
	p_loop.c
	p_user.c
	p_slopes.c
	p_sweep.cpp
	p_test.cpp
	p_deepcopy.cpp
	tables.c
	r_bsp.cpp
	r_data.c
	r_draw.cpp
	r_fps.c
	r_main.cpp
	r_plane.cpp
	r_segs.cpp
	r_skins.c
	r_voicepreference.cpp
	r_sky.c
	r_splats.c
	r_things.cpp
	r_bbox.c
	r_textures.c
	r_patch.c
	r_patchrotation.c
	r_picformats.c
	r_portal.c
	r_bbox.c
	r_debug.cpp
	r_debug_parser.cpp
	screen.c
	taglist.c
	v_video.cpp
	v_draw.cpp
	s_sound.c
	sounds.c
	w_wad.c
	filesrch.c
	mserv.c
	http-mserv.c
	i_tcp.c
	lzf.c
	lz4.c
	lua_script.c
	lua_baselib.c
	lua_mathlib.c
	lua_hooklib.c
	lua_consolelib.c
	lua_infolib.c
	lua_mobjlib.c
	lua_playerlib.c
	lua_skinlib.c
	lua_thinkerlib.c
	lua_maplib.c
	lua_taglib.c
	lua_polyobjlib.c
	lua_blockmaplib.c
	lua_botvarslib.c
	lua_hudlib.c
	lua_hudlib_drawlist.c
	lua_colorlib.c
	lua_followerlib.c
	lua_terrainlib.c
	lua_waypointslib.c
	lua_vectorlib.c
	lua_matrixlib.c
	lua_quaternionlib.c
	lua_voicelib.c
	lua_itemlib.c
	lua_profile.cpp
	k_kart.c
	k_collide.c
	k_color.c
	k_battle.c
	k_pwrlv.c
	k_waypoint.cpp
	k_pathfind.c
	k_bheap.c
	k_bot.cpp
	k_botitem.cpp
	k_botsearch.cpp
	k_cluster.cpp
	k_items.c
	k_itemlist.cpp
	k_grandprix.c
	k_boss.c
	k_hud.c
	k_hud_track.cpp
	k_hud_message.cpp
	k_terrain.c
	k_brightmap.c
	k_director.c
	k_follower.c
	k_mapuser.c
	k_stats.c
	k_specialstage.c
	k_vote.c
	k_dialogue.cpp
	h_timers.cpp
	radio_finish.cpp
	stun.c
	lonesha256.c
)

if (SRB2_CONFIG_DEDICATED)
	target_compile_definitions(BLANKART PRIVATE -DDEDICATED_BUILD)
endif()

if (SRB2_CONFIG_DEDICATED AND "${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
	target_compile_options(BLANKART PRIVATE -mconsole)
endif()

if(("${CMAKE_COMPILER_IS_GNUCC}" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang") AND "${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
	target_link_options(BLANKART PRIVATE "-Wl,--disable-dynamicbase")
	if("${SRB2_CONFIG_STATIC_STDLIB}")
		# On MinGW with internal libraries, link the standard library statically
		if(CMAKE_LINKER_TYPE STREQUAL "LLD")
			target_link_options(BLANKART PRIVATE -Wl, -static-libgcc -static-libstdc++ -static -lpthread)
		else()
			target_link_options(BLANKART PRIVATE -Wl,--add-stdcall-alias -static-libgcc -static-libstdc++ -static -lpthread)
		endif()
		set(THREADS_PREFER_PTHREAD_FLAG TRUE)
		find_package(Threads REQUIRED)
		target_link_libraries(BLANKART PRIVATE Threads::Threads)
	endif()
	if(CMAKE_SIZEOF_VOID_P EQUAL 4)
		target_link_options(BLANKART PRIVATE "-Wl,--large-address-aware")
	endif()
endif()

set(SRB2_CONFIG_STD23 OFF CACHE BOOL
	"Compiles the game under c23 and cxx23 instead c11 and cxx17.")

set(SRB2_CONFIG_COMPILEAPPIMAGE OFF CACHE BOOL
	"Compiles the game then attempts turning it into an appimage.")

set(SRB2_CONFIG_JEMALLOC OFF CACHE BOOL
	"Links jemalloc Allocator to replace GLibc malloc.")

if(SRB2_CONFIG_COMPILEAPPIMAGE AND "${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
message("-- Generating Appimage at build time.")
add_custom_command(
	TARGET BLANKART
	POST_BUILD
	WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin/
	COMMAND ${CMAKE_SOURCE_DIR}/appimage_build.sh -s
)
endif()

if(SRB2_CONFIG_STD23)
	message("-- Compiling game with C23 and CXX23.")
	target_compile_features(BLANKART PRIVATE c_std_23 cxx_std_23)
	set_property(TARGET BLANKART PROPERTY C_STANDARD 23)
	set_property(TARGET BLANKART PROPERTY CXX_STANDARD 23)
else()
	message("-- Compiling game with C11 and CXX17.")
	target_compile_features(BLANKART PRIVATE c_std_11 cxx_std_17)
	set_property(TARGET BLANKART PROPERTY C_STANDARD 11)
	set_property(TARGET BLANKART PROPERTY CXX_STANDARD 17)
endif()

# Thanks https://stackoverflow.com/a/63320046
if(SRB2_CONFIG_JEMALLOC)
	message("-- Using JEMALLOC.")
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(JEMALLOC jemalloc)

	pkg_search_module(JEMALLOC REQUIRED jemalloc)
	include_directories(${JEMALLOC_INCLUDE_DIRS})

	target_link_libraries(BLANKART PRIVATE ${JEMALLOC_LIBRARIES})
endif()

### Configuration
set(SRB2_CONFIG_DEV_BUILD OFF CACHE BOOL
	"Compile a development build of SRB2Kart.")

set(SRB2_CONFIG_SKIP_COMPTIME OFF CACHE BOOL
	"Skip Comptime rebuild.")

if(NOT SRB2_CONFIG_SKIP_COMPTIME)
	# This updates the modification time for comptime.c at the
	# end of building so when the build system is ran next time,
	# that file gets flagged. comptime.c will always be rebuilt.
	#
	# This begs the question, why always rebuild comptime.c?
	# Some things like the git commit must be checked each time
	# the program is built. But the build system determines which
	# files should be rebuilt before anything else. So
	# comptime.c, which only needs to be rebuilt based on
	# information known at build time, must be told to rebuild
	# before that information can be ascertained.
	add_custom_command(
		TARGET BLANKART
		POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E touch_nocreate ${CMAKE_CURRENT_SOURCE_DIR}/comptime.c
	)
endif()

set(GIT_EXECUTABLE "git" CACHE FILEPATH "Path to git binary")
add_custom_target(_SRB2_reconf ALL
	COMMAND ${CMAKE_COMMAND} -DGIT_EXECUTABLE=${GIT_EXECUTABLE} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/.. -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Comptime.cmake
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_dependencies(BLANKART _SRB2_reconf)

if(SRB2_CONFIG_ASAN)
	target_compile_options(BLANKART PRIVATE -fsanitize=address -fno-omit-frame-pointer)
	target_link_options(BLANKART PRIVATE -fsanitize=address -fno-omit-frame-pointer)
endif()

if(SRB2_CONFIG_UBSAN)
	target_compile_options(BLANKART PRIVATE -fsanitize=undefined)
	target_link_options(BLANKART PRIVATE -fsanitize=undefined)
endif()

if(SRB2_CONFIG_LSAN)
	target_compile_options(BLANKART PRIVATE -fsanitize=leak)
	target_link_options(BLANKART PRIVATE -fsanitize=leak)
endif()

if(SRB2_CONFIG_TSAN)
	target_compile_options(BLANKART PRIVATE -fsanitize=thread)
	target_link_options(BLANKART PRIVATE -fsanitize=thread)
endif()

if(SRB2_CONFIG_MSAN)
	target_compile_options(BLANKART PRIVATE -fsanitize=memory -fPIE -fno-omit-frame-pointer)
	target_link_options(BLANKART PRIVATE -fsanitize=memory -fPIE -pie -fno-omit-frame-pointer)
endif()

if(SRB2_CONFIG_TIDY)
	set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=*")
endif()

if(SRB2_CONFIG_NOVERIFYIWADS)
	target_compile_definitions(BLANKART PRIVATE -DNOVERIFYIWADS)
endif()

add_subdirectory(blua)
add_subdirectory(blan)

if (SRB2_CONFIG_DEDICATED)
	add_subdirectory(dedicated)
else()
	add_subdirectory(sdl)
endif()

add_subdirectory(objects)
add_subdirectory(core)
add_subdirectory(acs)
add_subdirectory(monocypher)

# OS macros
if (UNIX)
	target_compile_definitions(BLANKART PRIVATE -DUNIXCOMMON)
endif()

if(CMAKE_COMPILER_IS_GNUCC)
	find_program(OBJCOPY objcopy)
endif()

if("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
	target_compile_definitions(BLANKART PRIVATE -DLINUX)
	if(${SRB2_SYSTEM_BITS} EQUAL 64)
		target_compile_definitions(BLANKART PRIVATE -DLINUX64)
	endif()
endif()

if("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
	target_compile_definitions(BLANKART PRIVATE -DMACOSX)
endif()

if("${CMAKE_SYSTEM_NAME}" MATCHES "FreeBSD")
	target_compile_definitions(BLANKART PRIVATE -DFREEBSD)
	target_link_libraries(BLANKART PRIVATE execinfo)
endif()

if("${SRB2_CONFIG_USE_GME}" AND NOT SRB2_CONFIG_DEDICATED)
	target_link_libraries(BLANKART PRIVATE gme)
	target_compile_definitions(BLANKART PRIVATE -DHAVE_GME)
	if(NOT "${SRB2_CONFIG_SYSTEM_LIBRARIES}")
		# this sucks but gme doesn't use modern cmake to delineate public headers
		target_include_directories(BLANKART PRIVATE "${libgme_SOURCE_DIR}")
	endif()
endif()

if (NOT SRB2_CONFIG_DEDICATED)
	target_link_libraries(BLANKART PRIVATE openmpt)
	target_compile_definitions(BLANKART PRIVATE -DHAVE_OPENMPT)
endif()

target_link_libraries(BLANKART PRIVATE ZLIB::ZLIB PNG::PNG CURL::libcurl)
target_compile_definitions(BLANKART PRIVATE -DHAVE_ZLIB -DHAVE_PNG -DHAVE_CURL -D_LARGEFILE64_SOURCE)
target_sources(BLANKART PRIVATE apng.c)

target_link_libraries(BLANKART PRIVATE tcbrindle::span)
target_link_libraries(BLANKART PRIVATE fmt::fmt-header-only)
target_link_libraries(BLANKART PRIVATE Tracy::TracyClient)

if(NOT "${SRB2_CONFIG_SYSTEM_LIBRARIES}" AND NOT SRB2_CONFIG_DEDICATED)
target_link_libraries(BLANKART PRIVATE sndfile)
target_link_libraries(BLANKART PRIVATE Vorbis::vorbis Vorbis::vorbisenc)
target_link_libraries(BLANKART PRIVATE Opus::opus)
target_link_libraries(BLANKART PRIVATE Ogg::ogg)
endif()

if(SRB2_CONFIG_UPNP)
	target_compile_definitions(BLANKART PRIVATE -DHAVE_MINIUPNPC)
	target_link_libraries(BLANKART PRIVATE miniupnpc)
	if (WIN32)
		target_compile_definitions(BLANKART PRIVATE -DMINIUPNP_STATICLIB)
		target_link_libraries(BLANKART PRIVATE iphlpapi)
	endif()
endif()

target_link_libraries(BLANKART PRIVATE xxHash::xxhash)

target_compile_definitions(BLANKART PRIVATE -DUSE_STUN)
if(SRB2_CONFIG_ENABLE_DISCORDRPC)
	target_link_libraries(BLANKART PRIVATE DiscordRPC::DiscordRPC)
	target_compile_definitions(BLANKART PRIVATE -DHAVE_DISCORDRPC)
	target_sources(BLANKART PRIVATE discord.c)
endif()

set(SRB2_HAVE_THREADS ON)
target_compile_definitions(BLANKART PRIVATE -DHAVE_THREADS)

if(${SRB2_CONFIG_HAVE_ZLIB})
	if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
		set(ZLIB_FOUND ON)
		set(ZLIB_COMPAT ON)
		set(WITH_GTEST OFF)
		set(ZLIB_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/zlib-ng)
		if(${SRB2_SYSTEM_BITS} EQUAL 64)
			set(ZLIB_LIBRARIES "-L${CMAKE_SOURCE_DIR}/thirdparty/zlib-ng/win32 -lz64")
		else() # 32-bit
			set(ZLIB_LIBRARIES "-L${CMAKE_SOURCE_DIR}/thirdparty/zlib-ng/win32 -lz32")
		endif()
	else()
		find_package(ZLIB)
	endif()
	if(${ZLIB_FOUND})
		set(SRB2_HAVE_ZLIB ON)
		target_compile_definitions(BLANKART PRIVATE -DHAVE_ZLIB)
	else()
		message(WARNING "You have specified that ZLIB is available but it was not found. BlanKart may not compile correctly.")
	endif()
endif()

if(${SRB2_CONFIG_HAVE_PNG} AND ${SRB2_CONFIG_HAVE_ZLIB})
	if (${ZLIB_FOUND})
		if(${SRB2_CONFIG_USE_INTERNAL_LIBRARIES})
			set(PNG_FOUND ON)
			set(PNG_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/libpng-src)
			if(${SRB2_SYSTEM_BITS} EQUAL 64)
				set(PNG_LIBRARIES "-L${CMAKE_SOURCE_DIR}/thirdparty/libpng-src/projects -lpng64")
			else() # 32-bit
				set(PNG_LIBRARIES "-L${CMAKE_SOURCE_DIR}/thirdparty/libpng-src/projects -lpng32")
			endif()
		else()
			find_package(PNG)
		endif()
		if(${PNG_FOUND})
			set(SRB2_HAVE_PNG ON)
			target_compile_definitions(BLANKART PRIVATE -DHAVE_PNG)
			target_compile_definitions(BLANKART PRIVATE -D_LARGEFILE64_SOURCE)
			target_sources(BLANKART PRIVATE apng.c)
		else()
			message(WARNING "You have specified that PNG is available but it was not found. BlanKart may not compile correctly.")
		endif()
	endif()
endif()

if("${SRB2_CONFIG_HWRENDER}")
	target_compile_definitions(BLANKART PRIVATE -DHWRENDER)
	add_subdirectory(hardware)

	if("${SRB2_CONFIG_STATIC_OPENGL}")
		find_package(OpenGL)
		if(${OPENGL_FOUND})
			target_compile_definitions(BLANKART PRIVATE -DSTATIC_OPENGL)
		else()
			message(WARNING "You have specified static opengl but opengl was not found. Not setting HWRENDER.")
		endif()
	endif()
endif()

# TODO: build this with the game
if(${CMAKE_SYSTEM} MATCHES Windows AND ${CMAKE_C_COMPILER_ID} MATCHES "GNU" AND ${SRB2_SYSTEM_BITS} EQUAL 32)
	target_link_libraries(BLANKART PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/drmingw/lib/win32/libexchndl.a"
		"${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/drmingw/lib/win32/libmgwhelp.a"
	)
	target_include_directories(BLANKART PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/drmingw/include")
endif()

# Targets

# If using CCACHE, then force it.
# https://github.com/Cockatrice/Cockatrice/pull/3052/files
if ("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
	get_property(RULE_LAUNCH_COMPILE GLOBAL PROPERTY RULE_LAUNCH_COMPILE)
	if(RULE_LAUNCH_COMPILE)
		MESSAGE(STATUS "Force enabling CCache usage under macOS")
		# Set up wrapper scripts
		configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/launch-c.in   ${CMAKE_BINARY_DIR}/launch-c)
		configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/launch-cxx.in ${CMAKE_BINARY_DIR}/launch-cxx)
		execute_process(COMMAND chmod a+rx
			"${CMAKE_BINARY_DIR}/launch-c"
			"${CMAKE_BINARY_DIR}/launch-cxx")

		# Set Xcode project attributes to route compilation through our scripts
		set(CMAKE_XCODE_ATTRIBUTE_CC         "${CMAKE_BINARY_DIR}/launch-c")
		set(CMAKE_XCODE_ATTRIBUTE_CXX        "${CMAKE_BINARY_DIR}/launch-cxx")
		set(CMAKE_XCODE_ATTRIBUTE_LD         "${CMAKE_BINARY_DIR}/launch-c")
		set(CMAKE_XCODE_ATTRIBUTE_LDPLUSPLUS "${CMAKE_BINARY_DIR}/launch-cxx")
	endif()
endif()

# Compatibility flag with later versions of GCC
# We should really fix our code to not need this
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	target_compile_options(BLANKART PRIVATE -mno-ms-bitfields)
endif()

# TODO: fix prng and P_CreateBlockmap overflow at some point
target_compile_options(BLANKART PRIVATE -fwrapv)

if (${CMAKE_BUILD_TYPE} MATCHES Debug)
	target_compile_options(BLANKART PRIVATE -O0)
	target_compile_options(BLANKART PRIVATE -g)
else()
	target_compile_options(BLANKART PRIVATE -O3)
endif()

# Compiler warnings configuration
target_compile_options(BLANKART PRIVATE
	# Using generator expressions to handle per-language compile options

	# C, GNU
	# This is a direct translation from versions.mk
	$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:GNU>>:
		-Wall
		-Wextra
		-Wno-trigraphs
		-W # Was controlled by RELAXWARNINGS
		-Wfloat-equal
		-Wundef
		-Wpointer-arith
		-Wbad-function-cast
		-Wcast-qual
		-Wcast-align # Was controlled by NOCASTALIGNWARN
		-Wwrite-strings
		-Wsign-compare
		-Wno-comment
		-Wno-alloc-size-larger-than # holy shit, be quiet up GCC
		-Wmissing-prototypes
		-Wmissing-declarations
		-Wmissing-noreturn
		-Wnested-externs
		-Wformat-y2k
		-Wformat-security

		$<$<VERSION_LESS:$<C_COMPILER_VERSION>,2.9.5>:
			-Wno-div-by-zero
			-Wendif-labels
			-Wdisabled-optimization
		>

		$<$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,4.0.0>:
			-Wold-style-definition
			-Wmissing-field-initializers
		>

		$<$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,4.1.0>:
			-Wshadow
		>

		$<$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,4.3.0>:
			-funit-at-a-time
			-Wlogical-op
		>

		$<$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,4.5.0>:
			-Wlogical-op
			-Wno-error=array-bounds
		>

		$<$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,4.6.0>:
			-Wno-suggest-attribute=noreturn
			-Wno-error=suggest-attribute=noreturn
		>

		$<$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,5.4.0>:
			-Wno-logical-op
			-Wno-error=logical-op
		>

		$<$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,6.1.0>:
			-Wno-tautological-compare
			-Wno-error=tautological-compare
		>

		$<$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,7.1.0>:
			-Wno-error=format-overflow=2
			-Wimplicit-fallthrough=4
		>

		$<$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,8.1.0>:
			-Wno-error=format-overflow
			-Wno-error=stringop-truncation
			-Wno-error=stringop-overflow
			-Wno-format-overflow
			-Wno-stringop-truncation
			-Wno-stringop-overflow
			-Wno-error=multistatement-macros
		>

		$<$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,9.1.0>:
			-Wno-error=address-of-packed-member
		>
	>

	# C, Clang and Apple Clang
	$<$<AND:$<COMPILE_LANGUAGE:C>,$<OR:$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:Clang>>>:
		-Wall
		-Wextra
		-Wno-error=date-time
		-Wno-absolute-value
		-Wno-trigraphs
		-Wno-error=non-literal-null-conversion
		-Wno-error=constant-conversion
		-Wno-unused-but-set-variable
		-Wno-error=unused-but-set-variable
		-Wshadow
	>

	# C++, GNU, Clang and Apple Clang
	$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:Clang>>>:
		-Wall
		-Wno-error=date-time
		-Wno-unused-function
		-Wno-unused-but-set-variable
		-Wno-unused-private-field
	>

	# GNU
	$<$<C_COMPILER_ID:GNU>:
		-fmax-errors=5
	>
)
if(SRB2_CONFIG_ERRORMODE)
	target_compile_options(BLANKART PRIVATE
		$<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:Clang>>:
			-Werror
		>
	)
endif()

# Link warnings configuration
target_link_options(BLANKART PRIVATE
	$<$<C_COMPILER_ID:GNU>:
		# -Wl,--as-needed   - Was controlled by NOLDWARNING
	>
)

if(${SRB2_CONFIG_DEV_BUILD})
	target_compile_definitions(BLANKART PRIVATE -DDEVELOP)
endif()
target_compile_definitions(BLANKART PRIVATE -DCMAKECONFIG)

# Misc. build options from Makefiles
if(SRB2_CONFIG_DEBUGMODE)
	target_compile_definitions(BLANKART PRIVATE -D_DEBUG -DZDEBUG -DPARANOIA -DRANGECHECK -DPACKETDROP)
endif()
if(SRB2_CONFIG_MOBJCONSISTANCY)
	target_compile_definitions(BLANKART PRIVATE -DMOBJCONSISTANCY)
endif()
if(SRB2_CONFIG_PACKETDROP)
	target_compile_definitions(BLANKART PRIVATE -DPACKETDROP)
endif()
if(SRB2_CONFIG_ZDEBUG)
	target_compile_definitions(BLANKART PRIVATE -DZDEBUG)
endif()
if(SRB2_CONFIG_PROFILEMODE AND "${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
	target_compile_options(BLANKART PRIVATE -pg)
	target_link_options(BLANKART PRIVATE -pg)
endif()

if(SRB2_CONFIG_LTO AND NOT SRB2_CONFIG_DEBUGMODE)
	set_property(TARGET BLANKART PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# strip debug symbols into separate file when using gcc or clang.
# to be consistent with Makefile, don't generate for OS X.
if((CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")  AND NOT ("${CMAKE_SYSTEM_NAME}" MATCHES Darwin))
	if((${CMAKE_BUILD_TYPE} MATCHES Debug) OR (${CMAKE_BUILD_TYPE} MATCHES RelWithDebInfo))
		if(${CMAKE_BUILD_TYPE} MATCHES Debug)
			set(OBJCOPY_ONLY_KEEP_DEBUG "--only-keep-debug")
		endif()
		message(STATUS "Will make separate debug symbols in *.debug")
		add_custom_command(TARGET BLANKART POST_BUILD
			COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_ONLY_KEEP_DEBUG} $<TARGET_FILE:BLANKART> $<TARGET_FILE:BLANKART>.debug
			# mold linker: .gnu_debuglink is present by default, so --add-gnu-debuglink would fail
			COMMAND ${CMAKE_OBJCOPY} --strip-debug --remove-section=.gnu_debuglink $<TARGET_FILE:BLANKART>
			COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=$<TARGET_FILE:BLANKART>.debug $<TARGET_FILE:BLANKART>
		)
	endif()
endif()

# copy DLLs to bin/ directory if building internal shared on windows
if("${CMAKE_SYSTEM_NAME}" STREQUAL Windows AND NOT "${SRB2_CONFIG_INTERNAL_LIBRARIES}" AND "${SRB2_CONFIG_SHARED_INTERNAL_LIBRARIES}")
	set(ADDITIONAL_DLLS "")
	if("${CMAKE_C_COMPILER_ID}" STREQUAL GNU OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		# also copy implicitly linked system libraries
		get_filename_component(MINGW_BIN_PATH ${CMAKE_CXX_COMPILER} PATH)
		if("${CMAKE_C_COMPILER_ID}" STREQUAL GNU)
			list(APPEND ADDITIONAL_DLLS
				"libgcc_s_dw2-1.dll"
				"libstdc++-6.dll"
				"libwinpthread-1.dll"
			)
		else()
			list(APPEND ADDITIONAL_DLLS
				"libunwind.dll"
				"libc++.dll"
				"libwinpthread-1.dll"
			)
		endif()
		list(TRANSFORM ADDITIONAL_DLLS PREPEND "${MINGW_BIN_PATH}/")
	endif()
	add_custom_command(TARGET BLANKART POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_RUNTIME_DLLS:BLANKART>
			${ADDITIONAL_DLLS}

			$<TARGET_FILE_DIR:BLANKART>
		COMMAND_EXPAND_LISTS
		COMMENT "Copying runtime DLLs"
	)
endif()
