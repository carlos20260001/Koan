# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Lookout: camera snapshots (classic Media Kit) classified by libneural.
foreach(_lib neural media translation tracker localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Lookout: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	Lookout

	SOURCES
	Camera.cpp
	Engine.cpp
	Snapshot.cpp
	Lookout.cpp
	LookoutWindow.cpp
	PictureView.cpp
	ResultsView.cpp

	LIBS neural media translation tracker localestub
	RDEF Lookout.rdef
)

UseMediaKit(Lookout)
DoCatalogs("x-vnd.airos-Lookout" apps/lookout)
