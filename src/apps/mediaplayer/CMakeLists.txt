# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# MediaPlayer on the classic Media Kit (libmedia). media_server's PipeWire
# node add-on (src/add-ons/media/media-add-ons/pipewire) provides the audio
# output, and libmedia's decoder plugins (libavcodec and air/OS's hardware
# decoders such as nvdec, where installed) decode; MediaPlayer itself only
# talks to the kit. libmedia_controls registers it with
# media_controls_server for the media keys and the Deskbar replicant.

foreach(_lib media game tracker translation textencoding shared localestub
		media_controls)
	if(NOT TARGET ${_lib})
		message(STATUS "MediaPlayer: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	MediaPlayer

	SOURCES
	# interface
	interface/DurationView.cpp
	interface/LocationStringView.cpp
	interface/PeakView.cpp
	interface/PlayPauseButton.cpp
	interface/PositionToolTip.cpp
	interface/SeekSlider.cpp
	interface/SubtitleBitmap.cpp
	interface/SymbolButton.cpp
	interface/TransportControlGroup.cpp
	interface/VolumeSlider.cpp

	# media_node_framework
	media_node_framework/NodeManager.cpp
	media_node_framework/PlaybackListener.cpp
	media_node_framework/PlaybackLOAdapter.cpp
	media_node_framework/PlaybackManager.cpp

	# media_node_framework/audio
	media_node_framework/audio/AudioAdapter.cpp
	media_node_framework/audio/AudioChannelConverter.cpp
	media_node_framework/audio/AudioFormatConverter.cpp
	media_node_framework/audio/AudioProducer.cpp
	media_node_framework/audio/AudioReader.cpp
	media_node_framework/audio/AudioResampler.cpp
	media_node_framework/audio/AudioSupplier.cpp
	media_node_framework/audio/AudioVolumeConverter.cpp

	# media_node_framework/video
	media_node_framework/video/VideoConsumer.cpp
	media_node_framework/video/VideoProducer.cpp
	media_node_framework/video/VideoSupplier.cpp
	media_node_framework/video/VideoTarget.cpp

	# playlist
	playlist/CopyPLItemsCommand.cpp
	playlist/FilePlaylistItem.cpp
	playlist/ImportPLItemsCommand.cpp
	playlist/ListViews.cpp
	playlist/MovePLItemsCommand.cpp
	playlist/Playlist.cpp
	playlist/PlaylistItem.cpp
	playlist/PlaylistListView.cpp
	playlist/PlaylistObserver.cpp
	playlist/PlaylistFileReader.cpp
	playlist/PlaylistWindow.cpp
	playlist/PLItemsCommand.cpp
	playlist/RandomizePLItemsCommand.cpp
	playlist/RemovePLItemsCommand.cpp
	playlist/UrlPlaylistItem.cpp

	# settings
	settings/Settings.cpp
	settings/SettingsWindow.cpp

	# supplier
	supplier/AudioTrackSupplier.cpp
	supplier/ImageTrackVideoSupplier.cpp
	supplier/MediaFileTrackSupplier.cpp
	supplier/MediaTrackAudioSupplier.cpp
	supplier/MediaTrackVideoSupplier.cpp
	supplier/ProxyAudioSupplier.cpp
	supplier/ProxyVideoSupplier.cpp
	supplier/TrackSupplier.cpp
	supplier/SubTitles.cpp
	supplier/SubTitlesSRT.cpp
	supplier/VideoTrackSupplier.cpp

	# support
	support/AbstractLOAdapter.cpp
	support/ColorSpaceToString.cpp
	support/Command.cpp
	support/CommandStack.cpp
	support/DurationToString.cpp
	support/Event.cpp
	support/EventQueue.cpp
	support/FileReadWrite.cpp
	support/Listener.cpp
	support/ListenerAdapter.cpp
	support/MessageEvent.cpp
	support/Notifier.cpp
	support/StackBlurFilter.cpp

	# .
	Controller.cpp
	ControllerObserver.cpp
	ControllerView.cpp
	InfoWin.cpp
	MainApp.cpp
	MainWin.cpp
	NetworkStreamWin.cpp
	VideoView.cpp

	LIBS media game tracker translation textencoding localestub shared
		media_controls
	RDEF MediaPlayer.rdef
)

UseMediaKit(MediaPlayer)
UsePrivateHeaders(MediaPlayer interface shared)
# BEFORE: the global path holds src/kits/tracker, whose Settings.h would
# shadow settings/Settings.h.
target_include_directories(MediaPlayer BEFORE PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/interface
	${CMAKE_CURRENT_SOURCE_DIR}/media_node_framework
	${CMAKE_CURRENT_SOURCE_DIR}/media_node_framework/audio
	${CMAKE_CURRENT_SOURCE_DIR}/media_node_framework/video
	${CMAKE_CURRENT_SOURCE_DIR}/playlist
	${CMAKE_CURRENT_SOURCE_DIR}/settings
	${CMAKE_CURRENT_SOURCE_DIR}/supplier
	${CMAKE_CURRENT_SOURCE_DIR}/support
	# BRecentItems
	${PROJECT_SOURCE_DIR}/headers/os/be_apps/Tracker
	${PROJECT_SOURCE_DIR}/headers/private/media_controls
	${PROJECT_SOURCE_DIR}/headers/libs/agg
)
DoCatalogs("x-vnd.vos-MediaPlayer" apps/mediaplayer)
