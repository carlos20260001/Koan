# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# PoorMan serves files over plain BSD sockets (thttpd's libhttpd); on Linux
# those are glibc's, as libnetwork's were on air/OS.
foreach(_lib tracker shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "PoorMan: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

# mime_encodings.h and mime_types.h from the .txt tables, as air/OS's
# THTTPMakeHeader rule made them.
set(_poorman_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
foreach(_table mime_encodings mime_types)
	add_custom_command(
		OUTPUT "${_poorman_gen}/${_table}.h"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${_poorman_gen}"
		COMMAND sh -c "sed -e 's/#.*//' -e 's/[ \t]*$//' -e '/^$/d' -e 's/[ \t][ \t]*/\", 0, \"/' -e 's/^/{ \"/' -e 's/$/\", 0 },/' < \"$1\" > \"$2\""
			sh "${CMAKE_CURRENT_SOURCE_DIR}/libhttpd/${_table}.txt"
			"${_poorman_gen}/${_table}.h"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libhttpd/${_table}.txt"
		COMMENT "Generating PoorMan ${_table}.h"
		VERBATIM
	)
endforeach()

Application(
	PoorMan

	SOURCES
	PoorMan.cpp
	PoorManWindow.cpp
	PoorManView.cpp
	PoorManAdvancedView.cpp
	StatusSlider.cpp
	PoorManServer.cpp
	PoorManSiteView.cpp
	PoorManLogger.cpp
	PoorManLoggingView.cpp
	PoorManPreferencesWindow.cpp
	PoorManApplication.cpp
	constants.cpp

	# libhttpd
	libhttpd/libhttpd.c
	libhttpd/match.c
	libhttpd/tdate_parse.c
	"${_poorman_gen}/mime_encodings.h"
	"${_poorman_gen}/mime_types.h"

	LIBS tracker localestub
	RDEF PoorMan.rdef
)

target_include_directories(PoorMan PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/libhttpd" "${_poorman_gen}")
target_compile_definitions(PoorMan PRIVATE
	HAVE__PROGNAME=1 HAVE_FCNTL_H=1 HAVE_GRP_H=1 HAVE_MEMORY_H=1 HAVE_POLL_H=1
	HAVE_SYS_POLL_H=1 TIME_WITH_SYS_TIME=1 HAVE_DIRENT_H=1 HAVE_STRERROR=1
	HAVE_WAITPID=1 HAVE_VSNPRINTF=1 HAVE_SETSID=1 HAVE_SIGSET=1 HAVE_ATOLL=1
	HAVE_UNISTD_H=1 HAVE_GETPAGESIZE=1 HAVE_MMAP=1 HAVE_SELECT=1 HAVE_POLL=1
	HAVE_TM_GMTOFF=1 HAVE_SOCKLENT=1 HAVE_INT64T=1)
# air/OS's Jamfile turned -Werror off for this directory (thttpd code).
target_compile_options(PoorMan PRIVATE -Wno-error)
UsePrivateHeaders(PoorMan shared)
DoCatalogs("x-vnd.vos-PoorMan" apps/poorman)
