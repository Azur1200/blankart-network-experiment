add_subdirectory(ACSVM)
target_sources(BLANKART PRIVATE
	environment.cpp
	environment.hpp
	thread.cpp
	thread.hpp
	call-funcs.cpp
	call-funcs.hpp
	stream.cpp
	stream.hpp
	interface.cpp
	interface.h
)

