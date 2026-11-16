# libmedia_controls.a: the client side of media_controls_server (the
# system's now-playing service): players register a BMediaSession.
AddOn(media_controls
	STATIC

	SOURCES
	MediaSession.cpp
)

target_include_directories(media_controls PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/media_controls
)
# Linked into PIE executables and shared add-ons.
set_target_properties(media_controls PROPERTIES POSITION_INDEPENDENT_CODE ON)
