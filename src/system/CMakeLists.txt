# ported subset: libroot and libroot2 (milestone M4) supply every source of
# root; until they are here the target cannot be defined at all.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libroot2/CMakeLists.txt")

AddOn(
	root
	SHARED

	LIBS be_bootstrap rt dl pthread ${ZLIB_LIBRARIES} ${UDEV_LIBRARIES} ${BLKID_LIBRARIES}
)

# The build number comes from the environment, allocated by whatever ran
# the build. It is deliberately not read from a git tag here: the counter
# is not kept in this repo's history.
if(DEFINED ENV{VOS_PKG_REV} AND NOT "$ENV{VOS_PKG_REV}" STREQUAL "")
	set(VOS_REVISION "rev-$ENV{VOS_PKG_REV}")
else()
	set(VOS_REVISION "dev")
endif()
target_compile_definitions(root PRIVATE VOS_REVISION="${VOS_REVISION}")
message(STATUS "VOS revision:                    ${VOS_REVISION}")

find_library(BACKTRACE_LIBRARY backtrace)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT BACKTRACE_LIBRARY)
	message(FATAL_ERROR "Building in Debug mode requires libbacktrace-dev. Feel free to disable Debug build.")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	target_compile_definitions(root PRIVATE HAVE_LIBBACKTRACE)
	target_link_libraries(root PRIVATE ${BACKTRACE_LIBRARY})
	target_compile_options(root PRIVATE -fno-omit-frame-pointer)
	target_link_options(root PRIVATE -rdynamic)
endif()

# TODO: get rid of runtime loader
UsePrivateHeaders(root app shared libroot2 system kernel runtime_loader)

endif()

add_subdirectory(kernel)
# ported subset: libroot and libroot2 arrive in milestone M4.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libroot/CMakeLists.txt")
	add_subdirectory(libroot)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libroot2/CMakeLists.txt")
	add_subdirectory(libroot2)
endif()
