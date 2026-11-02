add_subdirectory(testharness)

Application(testinput SOURCES testinput.cpp)

Application(testxkbfuzz SOURCES testxkbfuzz.cpp LIBS shared)
UsePrivateHeaders(testxkbfuzz shared)

Application(testwatch SOURCES testwatch.cpp)

Application(testsuspend SOURCES testsuspend.cpp)

add_executable(testinject testinject.cpp)

# Unconfirmed display change revert state machine (pure helpers, the
# same sources app_server links; also runnable on a plain host).
add_executable(test_display_change_tracker
	test_display_change_tracker.cpp
	${CMAKE_SOURCE_DIR}/src/servers/app/DisplayChangeTracker.cpp
)
target_include_directories(test_display_change_tracker PRIVATE
	${CMAKE_SOURCE_DIR}/src/servers/app)

# Fractional screen coordinate and raster-boundary transforms.
Application(test_screen_scale_transform
	SOURCES test_screen_scale_transform.cpp
	INCLUDES ${CMAKE_SOURCE_DIR}/src/servers/app)
UsePrivateHeaders(test_screen_scale_transform interface)

# XDG desktop-entry and MIME-default discovery.
Application(test_xdg_discovery
	SOURCES test_xdg_discovery.cpp
	LIBS be)
UsePrivateHeaders(test_xdg_discovery app)

# Wayland event resources must only receive events for their owning client.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_SERVER QUIET IMPORTED_TARGET wayland-server)
pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
if(WAYLAND_SERVER_FOUND)
	Application(test_wayland_resource_owner
		SOURCES test_wayland_resource_owner.cpp
		LIBS PkgConfig::WAYLAND_SERVER
		INCLUDES ${CMAKE_SOURCE_DIR}/src/servers/app/wayland)
endif()
if(WAYLAND_SERVER_FOUND AND WAYLAND_CLIENT_FOUND)
	find_package(Threads REQUIRED)
	Application(test_wayland_data_device_manager
		SOURCES
			test_wayland_data_device_manager.cpp
			${CMAKE_SOURCE_DIR}/src/servers/app/wayland/WaylandDataDeviceManager.cpp
		LIBS PkgConfig::WAYLAND_SERVER PkgConfig::WAYLAND_CLIENT Threads::Threads
		INCLUDES ${CMAKE_SOURCE_DIR}/src/servers/app/wayland)
endif()

# Panel orientation rules (header only, no window server).
Application(test_panel_orientation
	SOURCES test_panel_orientation.cpp)
UsePrivateHeaders(test_panel_orientation interface)

# CUPS end-to-end test (V\OS printing, no print server).
Application(test_printcups
	SOURCES printcups/test_printcups.cpp
	LIBS be printcups ${CUPS_LIBRARIES})
UsePrivateHeaders(test_printcups print interface support)

#Application(test_node_monitor SOURCES test_node_monitor.cpp)
#Application(test_node_monitor2 SOURCES test_node_monitor2.cpp)

#UsePrivateHeaders(test_node_monitor kernel)
#UsePrivateHeaders(test_node_monitor2 kernel)
