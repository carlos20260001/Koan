# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# app_server's KMS display backend (drawing/interface/linux/kms).
#
# kms_core       the device, its outputs, layouts, scan-out buffers, page
#                flips and the GPU pass; no app_server code, so the tests
#                and the hardware harness link it alone
# app_server_kms KmsHWInterface, air/OS's HWInterface on kms_core; linked
#                into app_server (it needs app_server's objects)
#
# Everything is optional: without libdrm nothing here is built and
# VOS_HAVE_KMS_BACKEND stays undefined; without gbm/egl/glesv2 the backend
# has no GPU pass (VOS_HAVE_KMS_EGL undefined) and resamples on the CPU.
#
# Standalone (cmake -S this directory) it builds kms_core, the tests and the
# harness against the tree's headers, for testing without the whole tree.

cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(vos_kms_backend C CXX)
	set(KMS_STANDALONE ON)
	get_filename_component(VOS_ROOT
		"${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../.." ABSOLUTE)
	set(CMAKE_C_STANDARD 11)
	set(CMAKE_CXX_STANDARD 11)
	add_compile_definitions(__VOS__=1)
	add_compile_options(-Wall -Wno-multichar -fPIC)
	foreach(_dir
			headers headers/build headers/build/config_headers headers/config
			headers/os headers/os/support headers/os/kernel headers/os/app
			headers/os/interface headers/os/storage headers/os/drivers
			headers/os/add-ons/graphics headers/private headers/private/shared
			headers/private/graphics/common headers/private/graphics
			headers/private/device headers/private/app headers/private/interface
			headers/private/system headers/libs/agg)
		include_directories(AFTER "${VOS_ROOT}/${_dir}")
	endforeach()
	enable_testing()
	set(KMS_BUILD_TESTS ON)
else()
	set(KMS_STANDALONE OFF)
	set(VOS_ROOT "${PROJECT_SOURCE_DIR}")
	if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR VOS_BUILD_TESTS)
		set(KMS_BUILD_TESTS ON)
	else()
		set(KMS_BUILD_TESTS OFF)
	endif()
endif()

find_package(PkgConfig)
if(PkgConfig_FOUND)
	pkg_check_modules(KMS_LIBDRM IMPORTED_TARGET libdrm)
	pkg_check_modules(KMS_GBM IMPORTED_TARGET gbm)
	pkg_check_modules(KMS_EGL IMPORTED_TARGET egl)
	pkg_check_modules(KMS_GLESV2 IMPORTED_TARGET glesv2)
	pkg_check_modules(KMS_UDEV IMPORTED_TARGET libudev)
	pkg_check_modules(KMS_LIBSEAT IMPORTED_TARGET libseat)
endif()

option(VOS_KMS_BACKEND "Build app_server's KMS display backend" ON)
option(VOS_KMS_EGL "Give the KMS backend its GPU pass (EGL, GBM, GLES2)" ON)

if(NOT VOS_KMS_BACKEND OR NOT KMS_LIBDRM_FOUND)
	message(STATUS "KMS display backend: OFF (libdrm not found or disabled)")
	if(NOT KMS_STANDALONE)
		set(VOS_HAVE_KMS_BACKEND OFF PARENT_SCOPE)
	endif()
	return()
endif()

set(VOS_HAVE_KMS_BACKEND ON)
if(VOS_KMS_EGL AND KMS_GBM_FOUND AND KMS_EGL_FOUND AND KMS_GLESV2_FOUND)
	set(VOS_HAVE_KMS_EGL ON)
else()
	set(VOS_HAVE_KMS_EGL OFF)
endif()
message(STATUS "KMS display backend: ON (GPU pass: ${VOS_HAVE_KMS_EGL}, "
	"GBM: ${KMS_GBM_FOUND}, udev: ${KMS_UDEV_FOUND}, "
	"libseat: ${KMS_LIBSEAT_FOUND})")

set(KMS_CORE_SOURCES
	KmsBlit.cpp
	KmsEdid.cpp
	KmsLayout.cpp
	"${VOS_ROOT}/src/add-ons/accelerants/common/decode_edid.c"
)

add_library(kms_core STATIC ${KMS_CORE_SOURCES})
target_include_directories(kms_core PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${VOS_ROOT}/headers/private/graphics/common"
	"${VOS_ROOT}/headers/os/add-ons/graphics")
target_compile_definitions(kms_core PUBLIC VOS_HAVE_KMS_BACKEND=1)
target_link_libraries(kms_core PUBLIC PkgConfig::KMS_LIBDRM pthread)
set_property(TARGET kms_core PROPERTY POSITION_INDEPENDENT_CODE ON)
# decode_edid.c is C: the compatibility header is for C++ and C alike
target_compile_options(kms_core PRIVATE
	-include "${VOS_ROOT}/headers/build/LinuxBuildCompatibility.h")

if(KMS_BUILD_TESTS)
	add_subdirectory(tests)
endif()

# for app_server's CMakeLists
if(NOT KMS_STANDALONE)
	set(VOS_HAVE_KMS_BACKEND ${VOS_HAVE_KMS_BACKEND} PARENT_SCOPE)
	set(VOS_HAVE_KMS_EGL ${VOS_HAVE_KMS_EGL} PARENT_SCOPE)
endif()
