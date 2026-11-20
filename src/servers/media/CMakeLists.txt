# media_server: air/OS's classic Media Kit server (libmedia, MEDIA_1). It
# keeps the registry of nodes, buffers and defaults and talks to its
# clients through Nexus ports and areas, as on Haiku. The sound hardware is
# PipeWire, reached through the PipeWire node add-on
# (src/add-ons/media/media-add-ons/pipewire) that media_addon_server loads.
Server(
	media_server

	SOURCES
	media_server.cpp
	AppManager.cpp
	BufferManager.cpp
	DefaultManager.cpp
	MediaFilesManager.cpp
	NodeManager.cpp
	NotificationManager.cpp
	Queue.cpp

	LIBS media
	RDEF media_server.rdef
)

UseMediaKit(media_server)
VosPrivateHeaders(media_server app media shared storage system)
