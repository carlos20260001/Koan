# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# app_server's KMS display backend (drawing/interface/linux/kms).
#
# This is an include file: src/servers/app/CMakeLists.txt include()s it
# and compiles APP_SERVER_KMS_SRCS (relative to src/servers/app) into
# app_server, linked with APP_SERVER_KMS_LIBS, with APP_SERVER_KMS_INCLUDES;
# app_server then defines VOS_HAVE_KMS_BACKEND and calls
#	HWInterface* create_kms_hw_interface();
#
# Everything is optional: without libdrm APP_SERVER_KMS_SRCS stays empty;
# without gbm/egl/glesv2 there is no GPU pass (the CPU resamples), without
# libudev no hot plug events and no backlight, without libseat only
# janus_session's descriptor or a directly opened card.
#
# The tests (kms/tests: kms_core, kms_unit_test, kms_harness) are added
# from here in Debug builds or with VOS_BUILD_TESTS. Standalone
# (cmake -S <this directory>) it builds just those, against the tree's
# headers, for testing without the rest of the tree.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
	cmake_minimum_required(VERSION 3.25)
	project(vos_kms_backend C CXX)
	set(KMS_STANDALONE ON)
	get_filename_component(VOS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../../.."
		ABSOLUTE)
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

set(KMS_DIR "${CMAKE_CURRENT_LIST_DIR}")

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
	return()
endif()

set(KMS_DEFINITIONS VOS_HAVE_KMS_BACKEND=1)
set(KMS_LIBS PkgConfig::KMS_LIBDRM)
set(VOS_HAVE_KMS_EGL OFF)
if(KMS_GBM_FOUND)
	list(APPEND KMS_DEFINITIONS VOS_HAVE_KMS_GBM=1)
	list(APPEND KMS_LIBS PkgConfig::KMS_GBM)
	if(VOS_KMS_EGL AND KMS_EGL_FOUND AND KMS_GLESV2_FOUND)
		set(VOS_HAVE_KMS_EGL ON)
		list(APPEND KMS_DEFINITIONS VOS_HAVE_KMS_EGL=1)
		list(APPEND KMS_LIBS PkgConfig::KMS_EGL PkgConfig::KMS_GLESV2)
	endif()
endif()
if(KMS_UDEV_FOUND)
	list(APPEND KMS_DEFINITIONS VOS_HAVE_KMS_UDEV=1)
	list(APPEND KMS_LIBS PkgConfig::KMS_UDEV)
endif()
if(KMS_LIBSEAT_FOUND)
	list(APPEND KMS_DEFINITIONS VOS_HAVE_KMS_LIBSEAT=1)
	list(APPEND KMS_LIBS PkgConfig::KMS_LIBSEAT)
endif()
message(STATUS "KMS display backend: ON (GPU pass: ${VOS_HAVE_KMS_EGL}, "
	"GBM: ${KMS_GBM_FOUND}, udev: ${KMS_UDEV_FOUND}, "
	"libseat: ${KMS_LIBSEAT_FOUND})")

# the device, its outputs and buffers: no app_server code
set(KMS_CORE_NAMES KmsBlit KmsBuffer KmsDevice KmsEdid KmsLayout KmsSession)
if(VOS_HAVE_KMS_EGL)
	list(APPEND KMS_CORE_NAMES KmsGles)
endif()
set(KMS_CORE_SOURCES "")
foreach(_name ${KMS_CORE_NAMES})
	list(APPEND KMS_CORE_SOURCES "${KMS_DIR}/${_name}.cpp")
endforeach()

if(NOT KMS_STANDALONE)
	set(APP_SERVER_KMS_SRCS "")
	foreach(_name ${KMS_CORE_NAMES} KmsHWInterface)
		list(APPEND APP_SERVER_KMS_SRCS
			"drawing/interface/linux/kms/${_name}.cpp")
		set_source_files_properties("${KMS_DIR}/${_name}.cpp" PROPERTIES
			COMPILE_DEFINITIONS "${KMS_DEFINITIONS}")
	endforeach()
	# app_server compiles decode_edid.c and SplashHandoff.cpp itself
	set(APP_SERVER_KMS_LIBS ${KMS_LIBS})
	set(APP_SERVER_KMS_INCLUDES "${KMS_DIR}"
		"${VOS_ROOT}/headers/private/graphics/common")
endif()

if(KMS_BUILD_TESTS)
	add_subdirectory("${KMS_DIR}/tests" "${CMAKE_CURRENT_BINARY_DIR}/kms_tests")
endif()
