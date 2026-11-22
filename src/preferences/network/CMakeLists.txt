# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The network kit (libnetapi in libbe) keeps air/OS's API on NetworkManager;
# the configuration pages come from the network_settings add-ons.
Application(
	Network

	SOURCES
	Network.cpp
	NetworkWindow.cpp
	NetworkProfile.cpp
	NetworkSettingsAddOn.cpp
	InterfaceListItem.cpp
	InterfaceStatus.cpp
	InterfaceView.cpp
	ServiceOrderWindow.cpp
	ThroughputView.cpp

	# The Wi-Fi link details, shared with the WiFi preferences
	../wifi/WiFiController.cpp
	../wifi/LinuxWireless.cpp

	# Common functionality for add-ons
	IPAddressControl.cpp
	InterfaceAddressView.cpp
	ServiceListItem.cpp
	ServiceView.cpp

	# from NetworkStatus
	../../apps/networkstatus/RadioView.cpp
	../../apps/networkstatus/WirelessNetworkMenuItem.cpp
	# from ifconfig
	../../bin/network/ifconfig/MediaTypes.cpp

	LIBS shared localestub
	RDEF Network.rdef InterfaceIcons.rdef
)

UsePrivateHeaders(Network net shared storage)
# The network_settings add-ons subclass BNetworkSettingsAddOn and use the
# shared views compiled in here; they resolve them against this executable.
set_target_properties(Network PROPERTIES ENABLE_EXPORTS 1)
target_include_directories(Network PRIVATE
	${PROJECT_SOURCE_DIR}/headers/os/add-ons/network_settings
	${PROJECT_SOURCE_DIR}/src/apps/networkstatus
	${PROJECT_SOURCE_DIR}/src/bin/network/ifconfig
	${PROJECT_SOURCE_DIR}/src/preferences/wifi
)
# FreeBSD's if_media.h (media names for ifconfig's MediaTypes.cpp and the
# 802.11 PHY modes), headers only, as air/OS's Jamfile used it.
target_include_directories(Network PRIVATE
	${PROJECT_SOURCE_DIR}/src/libs/compat)
DoCatalogs("x-vnd.vos-Network" preferences/network)
