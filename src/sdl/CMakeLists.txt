# Declare SDL3 interface sources

target_sources(BLANKART PRIVATE
	ogl_sdl.cpp
	i_threads.c
	i_net.c
	i_gamepad.c
	i_system.cpp
	i_main.c
	i_video.cpp
	i_imgui.cpp
	dosstr.c
	endtxt.c
	hwsym_sdl.c
)

if("${CMAKE_SYSTEM_NAME}" MATCHES Windows)
	target_sources(BLANKART PRIVATE
		../win32/win_dbg.c
		../win32/Srb2win.rc)
endif()

if("${CMAKE_SYSTEM_NAME}" MATCHES Darwin)
	set(MACOSX_BUNDLE_ICON_FILE Srb2mac.icns)
	set_source_files_properties(macosx/Srb2mac.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
	target_sources(BLANKART PRIVATE
		macosx/mac_alert.c
		macosx/mac_alert.h
		macosx/mac_resources.c
		macosx/mac_resources.h
		macosx/Srb2mac.icns
	)
endif()

if("${CMAKE_SYSTEM_NAME}" MATCHES Darwin)
	find_library(CORE_FOUNDATION_LIBRARY "CoreFoundation")
	target_link_libraries(BLANKART PRIVATE
		${CORE_FOUNDATION_LIBRARY}
	)

	#target_link_libraries(BLANKART PRIVATE SRB2Core)
	set_target_properties(BLANKART PROPERTIES OUTPUT_NAME "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")

	# Configure the app bundle icon and plist properties
	target_sources(BLANKART PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/macosx/Srb2mac.icns")
	set_target_properties(BLANKART PROPERTIES
		MACOSX_BUNDLE_ICON_FILE "Srb2mac"
		MACOSX_BUNDLE_BUNDLE_NAME "BlanKart"
		MACOSX_BUNDLE_BUNDLE_VERSION ${SRB2_VERSION}

		RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/macosx/Srb2mac.icns"
	)
endif()

if("${CMAKE_SYSTEM_NAME}" MATCHES Linux)
	target_link_libraries(BLANKART PRIVATE m rt)
endif()

if("${CMAKE_SYSTEM_NAME}" MATCHES Windows)
	target_link_libraries(BLANKART PRIVATE
		ws2_32
	)
	#target_compile_options(BLANKART PRIVATE
	#	-U_WINDOWS
	#)
endif()

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3 REQUIRED)
target_link_libraries(BLANKART PRIVATE SDL3::SDL3)
if(TARGET SDL3main)
	target_link_libraries(BLANKART PRIVATE SDL3main)
endif()
target_compile_definitions(BLANKART PRIVATE -DDIRECTFULLSCREEN)
target_include_directories(BLANKART PRIVATE ${SDL3_INCLUDE_DIR})
target_compile_definitions(BLANKART PRIVATE -DHAVE_SDL)

find_package(OpenAL CONFIG QUIET)
find_package(OpenAL QUIET)

target_link_libraries(BLANKART PRIVATE sndfile)
target_compile_definitions(BLANKART PRIVATE -DHAVE_OPENAL)
target_link_libraries(BLANKART PRIVATE OpenAL::OpenAL)
target_sources(BLANKART PRIVATE al_sound.c)
if(NOT "${SRB2_CONFIG_SYSTEM_LIBRARIES}" AND NOT "${SRB2_CONFIG_SHARED_INTERNAL_LIBRARIES}")
	target_link_libraries(BLANKART PRIVATE SDL3::SDL3-static)
else()
	target_link_libraries(BLANKART PRIVATE SDL3::SDL3)
endif()

if(TARGET SDL3::SDL3)
	message(STATUS "SDL3 Found")
else()
	message(STATUS "no SDL3 Found")
endif()

if(TARGET SDL3::SDL3main)
	message(STATUS "SDL3main Found")
else()
	message(STATUS "No SDL3main Found")
endif()

if(TARGET OpenAL::OpenAL)
	message(STATUS "OpenAL Found")
else()
	message(FATAL_ERROR "no OpenAL Found")
endif()

#### Installation ####
if("${CMAKE_SYSTEM_NAME}" MATCHES Darwin)
	install(TARGETS BLANKART
		BUNDLE DESTINATION .
	)
	set_property(TARGET BLANKART PROPERTY INSTALL_RPATH_USE_LINK_PATH ON)
else()
	install(TARGETS BLANKART BLANKART
		RUNTIME DESTINATION .
	)
	if ((${CMAKE_BUILD_TYPE} MATCHES Debug) OR (${CMAKE_BUILD_TYPE} MATCHES RelWithDebInfo))
		set(SRB2_DEBUG_INSTALL OFF CACHE BOOL "Insert *.debug file into the install directory or package.")
		if (${SRB2_DEBUG_INSTALL})
			install(FILES $<TARGET_FILE:BLANKART>.debug
				DESTINATION .
				OPTIONAL
			)
		endif()
	endif()
endif()

# Mac bundle fixup
# HACK: THIS IS IMPORTANT! See the escaped \${CMAKE_INSTALL_PREFIX}? This
# makes it so that var is evaluated LATER during cpack, not right now!
# This fixes the quirk where the bundled libraries don't land in the final package
# https://cmake.org/pipermail/cmake/2011-March/043532.html
#
# HOWEVER: ${CPACK_PACKAGE_DESCRIPTION_SUMMARY} is NOT escaped, because that var
# is only available to us at this step. Read the link: ${CMAKE_INSTALL_PREFIX} at
# this current step points to the CMAKE build folder, NOT the folder that CPACK uses.
# Therefore, it makes sense to escape that var, but not the other.
if("${CMAKE_SYSTEM_NAME}" MATCHES Darwin)
	install(CODE "
		include(BundleUtilities)
		fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/${CPACK_PACKAGE_DESCRIPTION_SUMMARY}.app\"
			\"\"
			/Library/Frameworks
		)"
	)
endif()

set(BLANKART_AVAILABLE YES PARENT_SCOPE)
