# accessibility_server: air/OS's accessibility server (haiku#54): connects
# applications' accessibility bridges (BAccessible) to assistive
# technologies over BMessages; that part needs nothing from Linux.
# TODO(linux backend): an AT-SPI 2 bridge (org.a11y.atspi.* on the a11y
# bus) so that Orca and other Linux assistive technologies see air/OS
# applications, and air/OS's screen reader sees native Linux ones.
Server(
	accessibility_server

	SOURCES
	AccessibilityServer.cpp

	RDEF accessibility_server.rdef
)

VosPrivateHeaders(accessibility_server app interface)
