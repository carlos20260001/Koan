# index_server: air/OS's indexing server; loads the index_server add-ons
# (full text, audio tags) for each volume.
Server(
	index_server

	SOURCES
	CatchUpManager.cpp
	main.cpp
	IndexServer.cpp
	${PROJECT_SOURCE_DIR}/src/add-ons/index_server/IndexServerAddOn.cpp
	VolumeWatcher.cpp

	RDEF index_server.rdef
)

VosPrivateHeaders(index_server index_server shared storage app system)
