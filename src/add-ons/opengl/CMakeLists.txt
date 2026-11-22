# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# air/OS's OpenGL route (PORTING.md, M10 - air/OS Mesa (hgl + Zink)): Mesa's
# Haiku platform, built on Linux. It gives the BeAPI
#   libGL-hgl.so.1    BGLView, the renderer roster and the GL entry points,
#   libglapi-hgl.so.0 their dispatch table,
#   add-ons/opengl/Zink  the renderer: Zink on the Vulkan drivers, softpipe
#                     when there is no Vulkan device,
# in the vos-mesa-hgl package. Mesa is 3rdparty/mesa (X547/mesa, at the
# mesa-22.0.5 release) with air/OS's series
# (docs/x399-workstation/patches/mesa-hgl) and then Koan's
# (docs/x399-workstation/patches/mesa-hgl-koan) applied at build time.
#
# The two libraries carry a -hgl suffix so they never share a name with
# Debian's libGL.so.1 (glvnd), which X11 and Wayland clients keep using.

option(VOS_WITH_MESA_HGL
	"Build Mesa's BeAPI OpenGL (libGL-hgl and the Zink renderer)" ON)
if(NOT VOS_WITH_MESA_HGL)
	message(STATUS "Mesa hgl: OFF")
	return()
endif()

set(_mesa_src "${PROJECT_SOURCE_DIR}/3rdparty/mesa")
set(_mesa_series
	"${PROJECT_SOURCE_DIR}/docs/x399-workstation/patches/mesa-hgl"
	"${PROJECT_SOURCE_DIR}/docs/x399-workstation/patches/mesa-hgl-koan")

if(NOT EXISTS "${_mesa_src}/meson.build")
	message(STATUS "Mesa hgl: skipped, 3rdparty/mesa is not checked out "
		"(git submodule update --init 3rdparty/mesa)")
	return()
endif()
find_program(VOS_MESON meson)
find_program(VOS_PATCH patch)
if(NOT VOS_MESON OR NOT VOS_PATCH)
	message(STATUS "Mesa hgl: skipped, needs meson and patch")
	return()
endif()
foreach(_lib be root game translation)
	if(NOT TARGET ${_lib})
		message(STATUS "Mesa hgl: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

# The BeAPI SDK as this tree compiles against it: the include directories
# every target gets, __VOS__ and LinuxBuildCompatibility.h. Mesa compiles
# against the container's own system headers, so the chroot's are left out.
get_directory_property(_dirs INCLUDE_DIRECTORIES)
set(_sdk_args "-D__VOS__=1")
foreach(_dir IN LISTS _dirs)
	if(VITRUVIAN_CHROOT_PATH AND _dir MATCHES "^${VITRUVIAN_CHROOT_PATH}")
		continue()
	endif()
	if(NOT IS_ABSOLUTE "${_dir}")
		set(_dir "${PROJECT_SOURCE_DIR}/${_dir}")
	endif()
	list(APPEND _sdk_args "-I${_dir}")
endforeach()
list(APPEND _sdk_args "-includeLinuxBuildCompatibility.h")
list(JOIN _sdk_args "," _sdk_args)

set(_sdk_link_dirs "")
foreach(_lib be root game translation)
	list(APPEND _sdk_link_dirs "$<TARGET_FILE_DIR:${_lib}>")
endforeach()
# libbe's own dependencies, for the linker only.
if(TARGET media2)
	list(APPEND _sdk_link_dirs "$<TARGET_FILE_DIR:media2>")
endif()
list(REMOVE_DUPLICATES _sdk_link_dirs)
set(_sdk_link_args "")
foreach(_dir IN LISTS _sdk_link_dirs)
	list(APPEND _sdk_link_args "-L${_dir}")
endforeach()
list(JOIN _sdk_link_dirs ":" _rpath_link)
list(APPEND _sdk_link_args "-Xlinker" "-rpath-link=${_rpath_link}"
	"-lbe" "-lgame" "-ltranslation" "-lroot")
list(JOIN _sdk_link_args "," _sdk_link_args)

set(_work "${CMAKE_CURRENT_BINARY_DIR}/mesa")
set(_stage "${CMAKE_CURRENT_BINARY_DIR}/mesa-stage")
set(_lib_dir "${_stage}/usr/lib")
set(_outputs
	"${_lib_dir}/libGL-hgl.so.1.0.0"
	"${_lib_dir}/libglapi-hgl.so.0.0.0"
	"${_lib_dir}/libzinkpipe.so")

set(_patches "")
foreach(_dir IN LISTS _mesa_series)
	file(GLOB _dir_patches "${_dir}/*.patch")
	list(APPEND _patches ${_dir_patches})
endforeach()

# Mesa's own ninja decides what to rebuild; this only has to run when the
# SDK libraries or the series change, or when the outputs are missing.
add_custom_command(
	OUTPUT ${_outputs}
	COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/mesa-hgl.sh"
		"${_mesa_src}" "${_work}" "${_stage}"
		"${_sdk_args}" "${_sdk_link_args}"
		${_mesa_series}
	DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/mesa-hgl.sh"
		"${_mesa_src}/VERSION"
		${_patches}
		be root game translation
	COMMENT "Building Mesa hgl (libGL-hgl, Zink renderer)"
	USES_TERMINAL
	VERBATIM
)
add_custom_target(mesa_hgl ALL DEPENDS ${_outputs})

# What BeAPI programs link for BGLView and the GL entry points.
add_library(GL-hgl INTERFACE)
target_link_libraries(GL-hgl INTERFACE "${_lib_dir}/libGL-hgl.so.1.0.0")
target_link_options(GL-hgl INTERFACE "LINKER:-rpath-link,${_lib_dir}")
add_dependencies(GL-hgl mesa_hgl)

set(_lib_permissions
	OWNER_READ OWNER_WRITE OWNER_EXECUTE
	GROUP_READ GROUP_EXECUTE
	WORLD_READ WORLD_EXECUTE)
# Installed under their sonames: nothing links them by the unversioned name
# outside this build.
install(FILES "${_lib_dir}/libGL-hgl.so.1.0.0"
	DESTINATION /lib RENAME libGL-hgl.so.1
	PERMISSIONS ${_lib_permissions} COMPONENT mesa-hgl)
install(FILES "${_lib_dir}/libglapi-hgl.so.0.0.0"
	DESTINATION /lib RENAME libglapi-hgl.so.0
	PERMISSIONS ${_lib_permissions} COMPONENT mesa-hgl)
# Named as on air/OS. It is the only renderer installed: the roster
# instantiates the first add-on the directory lists, and on Linux that order
# is not defined, so softpipe stays inside this add-on instead of beside it.
install(FILES "${_lib_dir}/libzinkpipe.so"
	DESTINATION /system/add-ons/opengl RENAME Zink
	PERMISSIONS ${_lib_permissions} COMPONENT mesa-hgl)

file(STRINGS "${_mesa_src}/VERSION" _mesa_version LIMIT_COUNT 1)
list(LENGTH _patches _patch_count)
message(STATUS "Mesa hgl: ON (Mesa ${_mesa_version}, ${_patch_count} patches)")
