# mcp_server: air/OS's Model Context Protocol server, which lets an agent
# inspect and drive the system (files, teams, scripting, screenshots, logs).
find_package(ZLIB REQUIRED)

Server(
	mcp_server

	SOURCES
	MCPServer.cpp
	App.cpp
	Json.cpp
	Mcp.cpp
	Process.cpp
	Transport.cpp
	Util.cpp
	DriverTools.cpp
	FileTools.cpp
	HealthTools.cpp
	InventoryTools.cpp
	PackageTools.cpp
	RunTools.cpp
	ScreenshotTools.cpp
	ScriptingTools.cpp
	SyslogTools.cpp
	TeamTools.cpp

	LIBS media ${ZLIB_LIBRARIES}
	RDEF mcp_server.rdef
)

# mcp_server.rdef imports mcp.hvif; rc runs in the build directory.
configure_file(mcp.hvif "${CMAKE_CURRENT_BINARY_DIR}/mcp.hvif" COPYONLY)

UseMediaKit(mcp_server)
VosPrivateHeaders(mcp_server app shared)
target_compile_options(mcp_server PRIVATE -std=c++17)
