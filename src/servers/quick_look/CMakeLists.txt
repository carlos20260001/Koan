# quick_look_server: air/OS's Quick Look previews and thumbnails.
Server(
	quick_look_server

	SOURCES
	AddOnManager.cpp
	GenericPreview.cpp
	PreviewWindow.cpp
	QuickLookServer.cpp
	Thumbnailer.cpp

	LIBS game media quicklook localestub
	RDEF quick_look_server.rdef
)

UseMediaKit(quick_look_server)
VosPrivateHeaders(quick_look_server app interface quicklook shared)
target_include_directories(quick_look_server PRIVATE
	"${PROJECT_SOURCE_DIR}/headers/os/add-ons/quick_look")
DoCatalogs("x-vnd.vos-quick_look_server" servers/quick_look)
