target_sources(BLANKART PRIVATE
	i_threads.c
	i_net.c
	i_gamepad.c
	i_system.cpp
	i_main.c
	i_video.c
	i_sound.c
	endtxt.c
	dosstr.c
)

if(${SRB2_HAVE_LIBBACKTRACE})
	target_link_libraries(BLANKART PRIVATE ${LIBBACKTRACE_LIBRARIES})
endif()
