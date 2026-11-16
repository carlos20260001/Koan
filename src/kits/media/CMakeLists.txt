# libmedia: air/OS's Media Kit (Haiku's BMediaRoster/BMediaNode/BBuffer*
# API plus air/OS's additions), hosted on Linux. It talks to media_server
# and media_addon_server through Nexus ports and areas, as on Haiku, and
# loads codec plugins and node add-ons from the add-ons directory.
#
# VitruvianOS's own kit, media2 (src/kits/media2), sits next to it: a
# different API on PipeWire/GStreamer, always included as <media2/...>.
# Both define classes with the same names (BMediaNode, BMediaFile,
# BSoundPlayer, ...), so:
#  - headers: the classic headers are in headers/os/media and must come
#    before headers/os/media2 on the include path (UseMediaKit below);
#  - symbols: each library exports its symbols under its own version node
#    (media.ver, media2.ver), so one process can load both - an air/OS
#    application on libmedia that also uses libgame (on media2) - without
#    one library's references binding to the other's classes.

set(VOS_MEDIA_HEADERS
	"${CMAKE_SOURCE_DIR}/headers/os/media"
	"${CMAKE_SOURCE_DIR}/headers/private/media"
	"${CMAKE_SOURCE_DIR}/headers/private/media/experimental"
)

# Put the classic Media Kit headers first for <target>, ahead of the global
# headers/os/media2. For libmedia and every application or add-on that uses
# the classic API (#include <MediaRoster.h>, <MediaDefs.h>, ...).
function(UseMediaKit target)
	target_include_directories(${target} BEFORE PRIVATE ${VOS_MEDIA_HEADERS})
endfunction()

AddOn(media
	SHARED

	SOURCES
	# Public Media Kit
	Buffer.cpp
	BufferConsumer.cpp
	BufferGroup.cpp
	BufferProducer.cpp
	Controllable.cpp
	FileInterface.cpp
	MediaAddOn.cpp
	MediaDecoder.cpp
	MediaDefs.cpp
	MediaEncoder.cpp
	MediaEventLooper.cpp
	MediaFile.cpp
	MediaFiles.cpp
	MediaFormats.cpp
	MediaNode.cpp
	MediaRecorder.cpp
	MediaRoster.cpp
	MediaTheme.cpp
	MediaTrack.cpp
	ParameterWeb.cpp
	PlaySound.cpp
	RealtimeAlloc.cpp
	Sound.cpp
	SoundFile.cpp
	SoundPlayer.cpp
	TimeCode.cpp
	TimedEventQueue.cpp
	TimeSource.cpp

	# Experimental Media Kit
	experimental/AdapterIO.cpp
	experimental/MediaClient.cpp
	experimental/MediaClientDefs.cpp
	experimental/MediaClientNode.cpp
	experimental/MediaConnection.cpp
	experimental/MediaIO.cpp
	experimental/SimpleMediaClient.cpp

	# Internal Functionality
	AddOnManager.cpp
	BufferCache.cpp
	DataExchange.cpp
	DefaultMediaTheme.cpp
	DormantNodeManager.cpp
	FormatManager.cpp
	MediaRecorderNode.cpp
	Notifications.cpp
	PortPool.cpp
	request_data.cpp
	SharedBufferList.cpp
	TrackReader.cpp
	TimeSourceObject.cpp
	TimeSourceObjectManager.cpp
	SoundPlayNode.cpp

	# Old (R3) Media Kit
	legacy/OldAudioModule.cpp
	legacy/OldAudioStream.cpp
	legacy/OldBufferStream.cpp
	legacy/OldBufferStreamManager.cpp
	legacy/OldMediaModule.cpp
	legacy/OldSubscriber.cpp

	# Codec Plugin API
	ChunkCache.cpp
	DecoderPlugin.cpp
	EncoderPlugin.cpp
	MediaExtractor.cpp
	MediaPlugin.cpp
	MediaStreamer.cpp
	MediaWriter.cpp
	PluginManager.cpp
	ReaderPlugin.cpp
	StreamerPlugin.cpp
	WriterPlugin.cpp

	# Compat Media Kit
	legacy/!missing_symbols.cpp
	legacy/Deprecated.cpp

	LIBS be root

	RDEF
	libmedia.rdef
)

UseMediaKit(media)
UsePrivateHeaders(media app shared interface support storage)
target_include_directories(media PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/experimental
	${CMAKE_CURRENT_SOURCE_DIR}/legacy
)
target_compile_options(media PRIVATE -std=c++17)

set_property(TARGET media APPEND PROPERTY LINK_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/media.ver")
target_link_options(media PRIVATE
	"-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/media.ver")

# libbe's companions (shared, localestub) once they have targets, as for be.
function(_vos_media_link_companions)
	foreach(_lib localestub shared)
		if(TARGET ${_lib})
			target_link_libraries(media PRIVATE ${_lib})
		endif()
	endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
	CALL _vos_media_link_companions)

DoCatalogs("x-vnd.vos-libmedia" kits/media)

# CTest (run ctest in this build directory): libmedia and libmedia2 keep to
# their own symbol versions, and the kits on media2 bind to media2. The
# consumers are defined after this directory, hence generator expressions.
enable_testing()
set(_vos_media2_consumers)
foreach(_lib game midi midi2)
	list(APPEND _vos_media2_consumers
		"$<$<TARGET_EXISTS:${_lib}>:$<TARGET_FILE:${_lib}>>")
endforeach()
add_test(NAME media_symbol_versions
	COMMAND sh
		"${CMAKE_SOURCE_DIR}/src/tests/kits/media/check_symbol_versions.sh"
		"${CMAKE_OBJDUMP}"
		"$<TARGET_FILE:media>"
		"$<$<TARGET_EXISTS:media2>:$<TARGET_FILE:media2>>"
		${_vos_media2_consumers})
