# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# NVPlay: air/OS's film player. It reads the file through the classic
# Media Kit (BMediaFile/BMediaTrack), so it gets whichever decoder libmedia
# picks: air/OS's nvdec add-on (NVIDIA's video engine) where it is
# installed, libavcodec otherwise. The player has no NVIDIA code itself.

foreach(_lib media localestub tracker)
	if(NOT TARGET ${_lib})
		message(STATUS "NVPlay: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	NVPlay

	SOURCES
	NVPlay.cpp

	LIBS media localestub tracker
	RDEF NVPlay.rdef
)

UseMediaKit(NVPlay)
