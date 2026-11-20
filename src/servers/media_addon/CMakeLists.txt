# media_addon_server: loads air/OS's classic media node add-ons (libmedia)
# for media_server; on Linux the PipeWire node add-on stands in for the
# sound card drivers.
Server(
	media_addon_server

	SOURCES
	MediaAddonServer.cpp
	MediaFilePlayer.cpp
	SystemTimeSource.cpp

	LIBS media
	RDEF media_addon_server.rdef
)

UseMediaKit(media_addon_server)
VosPrivateHeaders(media_addon_server media shared storage system)
