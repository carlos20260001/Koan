# net_server. On Linux NetworkManager configures the interfaces, runs DHCP
# and IPv6 autoconfiguration and joins wireless networks, and the network
# kit talks to it directly (libnetapi's NMBackend). NetServerLinux.cpp keeps
# what air/OS's preferences and kit ask the net_server for: the saved
# wireless networks, joining/leaving through NetworkManager, auto-joining
# saved networks, and the network services (Services.cpp).
# NetServer.cpp, AutoconfigLooper.cpp, AutoconfigClient.cpp, DHCPClient.cpp
# and PPPServer.cpp drive Haiku's network stack (SIOC* ioctls, net80211,
# /dev/net) and stay in the tree unbuilt.
Server(
	net_server

	SOURCES
	NetServerLinux.cpp
	Services.cpp

	LIBS shared
	RDEF net_server.rdef
)

VosPrivateHeaders(net_server app net shared storage)
target_include_directories(net_server PRIVATE
	"${PROJECT_SOURCE_DIR}/headers/os/add-ons/network_settings")
