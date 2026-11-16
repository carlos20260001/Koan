# Helpers for the Haiku GPU kernel drivers (src/add-ons/kernel/drivers/graphics)
# and their accelerants (src/add-ons/accelerants). Included only when
# VOS_WITH_HAIKU_GPU_DRIVERS is ON.
#
# These targets mirror the Jamfiles the directories had. The sources are
# Haiku's, unchanged: the kernel drivers need a Haiku kernel (its headers,
# module API and kernel.so), which this tree no longer has, so they configure
# but do not compile here. Nothing is installed: no image can load them.
#
# Target names:
#   accelerants      the Jam name, e.g. "radeon_hd.accelerant"
#   kernel drivers   "haiku_driver_<jam name>", output file "<jam name>"
#   static libs      "haiku_gpu_<name>" for Jam's "lib<name>.a"

set(HAIKU_GPU_PRIVATE_HEADERS "${PROJECT_SOURCE_DIR}/headers/private")

if(VITRUVIAN_EFFECTIVE_ARCH STREQUAL "arm64")
	set(_haiku_gpu_kernel_arch arm64)
	set(_haiku_gpu_system_arch arm64)
else()
	set(_haiku_gpu_kernel_arch x86)
	set(_haiku_gpu_system_arch x86_64)
endif()

# Jam's UsePrivateKernelHeaders
set(HAIKU_GPU_KERNEL_HEADERS
	"${HAIKU_GPU_PRIVATE_HEADERS}/kernel"
	"${HAIKU_GPU_PRIVATE_HEADERS}/kernel/arch/${_haiku_gpu_kernel_arch}"
	"${HAIKU_GPU_PRIVATE_HEADERS}/libroot"
	"${HAIKU_GPU_PRIVATE_HEADERS}/shared"
	"${HAIKU_GPU_PRIVATE_HEADERS}/system"
	"${HAIKU_GPU_PRIVATE_HEADERS}/system/arch/${_haiku_gpu_system_arch}"
)

# Jam's UsePrivateSystemHeaders
set(HAIKU_GPU_SYSTEM_HEADERS
	"${HAIKU_GPU_PRIVATE_HEADERS}/system"
	"${HAIKU_GPU_PRIVATE_HEADERS}/system/arch/${_haiku_gpu_system_arch}"
)

# Resolve sources the way Jam's SEARCH_SOURCE does: the current directory
# first, then each SEARCH directory. A source that no longer exists anywhere
# (its directory was deleted with the Haiku kernel) is reported and left out,
# so that configuring does not fail.
function(_HaikuGpuSources outvar)
	cmake_parse_arguments(_S "" "" "SOURCES;SEARCH" ${ARGN})
	set(_found)
	foreach(_src IN LISTS _S_SOURCES)
		set(_path)
		foreach(_dir "${CMAKE_CURRENT_SOURCE_DIR}" ${_S_SEARCH})
			if(EXISTS "${_dir}/${_src}")
				set(_path "${_dir}/${_src}")
				break()
			endif()
		endforeach()
		if(_path)
			list(APPEND _found "${_path}")
		else()
			message(WARNING "Haiku GPU: ${_src} (listed in "
				"${CMAKE_CURRENT_SOURCE_DIR}) no longer exists; left out")
		endif()
	endforeach()
	set(${outvar} ${_found} PARENT_SCOPE)
endfunction()

function(_HaikuGpuCommon name)
	cmake_parse_arguments(_C "" "" "INCLUDES;DEFINES;CFLAGS;CXXFLAGS" ${ARGN})
	target_include_directories(${name} PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}" "${HAIKU_GPU_PRIVATE_HEADERS}"
		${_C_INCLUDES})
	if(_C_DEFINES)
		target_compile_definitions(${name} PRIVATE ${_C_DEFINES})
	endif()
	foreach(_flag IN LISTS _C_CFLAGS)
		target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${_flag}>")
	endforeach()
	foreach(_flag IN LISTS _C_CXXFLAGS)
		target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${_flag}>")
	endforeach()
endfunction()

# Libraries that are not built yet (libbe before the kits wave) make the
# target be skipped with a note instead of failing at link time.
function(_HaikuGpuMissingLibs outvar)
	set(_missing)
	foreach(_lib IN LISTS ARGN)
		if(_lib STREQUAL "be" AND NOT TARGET be)
			list(APPEND _missing be)
		endif()
	endforeach()
	set(${outvar} ${_missing} PARENT_SCOPE)
endfunction()

set(_HAIKU_GPU_ARGS SOURCES SEARCH LIBS INCLUDES DEFINES CFLAGS CXXFLAGS)

# Jam's KernelAddon
function(HaikuGpuKernelAddon name)
	cmake_parse_arguments(_K "" "" "${_HAIKU_GPU_ARGS}" ${ARGN})
	_HaikuGpuSources(_srcs SOURCES ${_K_SOURCES} SEARCH ${_K_SEARCH})
	set(_target haiku_driver_${name})
	add_library(${_target} MODULE ${_srcs})
	set_target_properties(${_target} PROPERTIES
		OUTPUT_NAME "${name}" PREFIX "" SUFFIX "")
	_HaikuGpuCommon(${_target}
		INCLUDES ${HAIKU_GPU_KERNEL_HEADERS} ${_K_INCLUDES}
		DEFINES _KERNEL_MODE ${_K_DEFINES}
		CFLAGS ${_K_CFLAGS}
		CXXFLAGS -fno-exceptions ${_K_CXXFLAGS})
	target_link_options(${_target} PRIVATE -nostdlib)
	if(_K_LIBS)
		target_link_libraries(${_target} PRIVATE ${_K_LIBS})
	endif()
endfunction()

# Jam's KernelStaticLibrary and StaticLibrary; KERNEL selects the former.
# Position independent, since accelerants (shared add-ons) link them too.
function(HaikuGpuStaticLibrary name)
	cmake_parse_arguments(_L "KERNEL" "" "${_HAIKU_GPU_ARGS}" ${ARGN})
	_HaikuGpuSources(_srcs SOURCES ${_L_SOURCES} SEARCH ${_L_SEARCH})
	set(_target haiku_gpu_${name})
	add_library(${_target} STATIC ${_srcs})
	set_target_properties(${_target} PROPERTIES
		OUTPUT_NAME "${name}" POSITION_INDEPENDENT_CODE ON)
	if(_L_KERNEL)
		_HaikuGpuCommon(${_target}
			INCLUDES ${HAIKU_GPU_KERNEL_HEADERS} ${_L_INCLUDES}
			DEFINES _KERNEL_MODE ${_L_DEFINES}
			CFLAGS ${_L_CFLAGS}
			CXXFLAGS -fno-exceptions ${_L_CXXFLAGS})
	else()
		_HaikuGpuCommon(${_target}
			INCLUDES ${_L_INCLUDES} DEFINES ${_L_DEFINES}
			CFLAGS ${_L_CFLAGS} CXXFLAGS ${_L_CXXFLAGS})
		target_compile_options(${_target} PRIVATE
			-include LinuxBuildCompatibility.h)
	endif()
endfunction()

# Jam's Addon for an accelerant: a shared add-on with a bare file name.
function(HaikuGpuAccelerant name)
	cmake_parse_arguments(_A "" "" "${_HAIKU_GPU_ARGS}" ${ARGN})
	_HaikuGpuMissingLibs(_missing ${_A_LIBS})
	if(_missing)
		message(STATUS "Haiku GPU: ${name} skipped, needs ${_missing}")
		return()
	endif()
	_HaikuGpuSources(_srcs SOURCES ${_A_SOURCES} SEARCH ${_A_SEARCH})
	add_library(${name} MODULE ${_srcs})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	_HaikuGpuCommon(${name}
		INCLUDES ${_A_INCLUDES} DEFINES ${_A_DEFINES}
		CFLAGS ${_A_CFLAGS} CXXFLAGS ${_A_CXXFLAGS})
	# Same prelude as engine.cmake's AddOn for Linux-hosted userland.
	target_compile_options(${name} PRIVATE -include LinuxBuildCompatibility.h)
	if(_A_LIBS)
		target_link_libraries(${name} PRIVATE ${_A_LIBS})
	endif()
endfunction()

# Jam's "Depends <a> : <b>", when both targets were created.
function(HaikuGpuDepends target dependency)
	if(TARGET ${target} AND TARGET ${dependency})
		add_dependencies(${target} ${dependency})
	endif()
endfunction()
