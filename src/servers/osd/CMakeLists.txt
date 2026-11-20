# osd_server: air/OS's on-screen display for volume and brightness levels.
Server(
	osd_server

	SOURCES
	LevelIcons.cpp
	LevelView.cpp
	LevelWindow.cpp
	OSDServer.cpp

	LIBS localestub
	RDEF osd_server.rdef
)

VosPrivateHeaders(osd_server app interface osd)
DoCatalogs("x-vnd.vos-osd_server" servers/osd)
