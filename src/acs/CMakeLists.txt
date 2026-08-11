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


target_include_directories(BLANKART PRIVATE ACSVM) # This sucks

# This breaks Apple Clang 14 compile. It should be totally
# unecessary since even though vm/CMakeLists.txt sets
# CMAKE_CXX_FLAGS, it is in a lower scope.
#set(ACSVM_NOFLAGS ON)

set(ACSVM_SHARED OFF)
set(ACSVM_INSTALL_API OFF)
set(ACSVM_INSTALL_DOC OFF)
set(ACSVM_INSTALL_EXE OFF)
set(ACSVM_INSTALL_LIB OFF)
add_subdirectory(ACSVM)

target_link_libraries(BLANKART PRIVATE acsvm)
target_include_directories(acsvm PRIVATE "${CMAKE_BINARY_DIR}/src" "${CMAKE_BINARY_DIR}/$<CONFIG>/src")
