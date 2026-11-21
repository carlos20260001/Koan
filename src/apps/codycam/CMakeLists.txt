# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# CodyCam takes its frames from the classic Media Kit's default video input
# (BMediaRoster::GetVideoInput, served by media_server's node add-ons) and
# uploads them with libbe's network kit (BNetEndpoint) or sftp in a pty.
foreach(_lib media translation shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "CodyCam: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	CodyCam

	SOURCES
	CodyCam.cpp
	FileUploadClient.cpp
	SpawningUploadClient.cpp
	FtpClient.cpp
	SftpClient.cpp
	Settings.cpp
	VideoConsumer.cpp

	LIBS media shared translation localestub
	RDEF CodyCam.rdef
)

UseMediaKit(CodyCam)
UsePrivateHeaders(CodyCam shared)
DoCatalogs("x-vnd.vos-CodyCam" apps/codycam)
