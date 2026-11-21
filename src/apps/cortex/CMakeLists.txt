# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# Cortex, the classic Media Kit node graph: the Cortex application, its
# add-on host (CortexAddOnHost, which instantiates dormant nodes out of
# process for Cortex's NodeManager) and three example media add-ons that
# media_addon_server loads. The libraries of air/OS's Jamfiles
# (cortex_*.a, one per subdirectory) are two static libraries here.
# CORTEX_NAMESPACE stays undefined, as in air/OS: its Jamfiles set it with
# SubDirCcFlags, which reaches C sources only, and Cortex is all C++.
foreach(_lib media tracker translation localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Cortex: skipped, needs ${_lib}")
		return()
	endif()
endforeach()
find_package(EXPAT)
if(NOT EXPAT_FOUND)
	message(STATUS "Cortex: skipped, needs expat (libexpat1-dev)")
	return()
endif()

set(_cortex_dirs
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/DiagramView
	${CMAKE_CURRENT_SOURCE_DIR}/DormantNodeView
	${CMAKE_CURRENT_SOURCE_DIR}/InfoView
	${CMAKE_CURRENT_SOURCE_DIR}/MediaRoutingView
	${CMAKE_CURRENT_SOURCE_DIR}/NodeManager
	${CMAKE_CURRENT_SOURCE_DIR}/ParameterView
	${CMAKE_CURRENT_SOURCE_DIR}/Persistence
	${CMAKE_CURRENT_SOURCE_DIR}/Persistence/Wrappers
	${CMAKE_CURRENT_SOURCE_DIR}/RouteApp
	${CMAKE_CURRENT_SOURCE_DIR}/TipManager
	${CMAKE_CURRENT_SOURCE_DIR}/TransportView
	${CMAKE_CURRENT_SOURCE_DIR}/ValControl
	${CMAKE_CURRENT_SOURCE_DIR}/support
)

# cortex_support.a, alone: CortexAddOnHost links only this part.
add_library(cortex_support STATIC
	support/MediaIcon.cpp
	support/MediaString.cpp
	support/MouseTrackingHelpers.cpp
	support/MultiInvoker.cpp
	support/ObservableHandler.cpp
	support/ObservableLooper.cpp
	support/observe.cpp
	support/SoundUtils.cpp
	support/TextControlFloater.cpp
)

# The other cortex_*.a libraries of the application.
add_library(cortex_views STATIC
	DiagramView/DiagramBox.cpp
	DiagramView/DiagramEndPoint.cpp
	DiagramView/DiagramItem.cpp
	DiagramView/DiagramItemGroup.cpp
	DiagramView/DiagramView.cpp
	DiagramView/DiagramWire.cpp

	DormantNodeView/DormantNodeListItem.cpp
	DormantNodeView/DormantNodeView.cpp
	DormantNodeView/DormantNodeWindow.cpp

	InfoView/AppNodeInfoView.cpp
	InfoView/ConnectionInfoView.cpp
	InfoView/DormantNodeInfoView.cpp
	InfoView/EndPointInfoView.cpp
	InfoView/FileNodeInfoView.cpp
	InfoView/InfoView.cpp
	InfoView/InfoWindow.cpp
	InfoView/InfoWindowManager.cpp
	InfoView/LiveNodeInfoView.cpp

	MediaRoutingView/MediaJack.cpp
	MediaRoutingView/MediaNodePanel.cpp
	MediaRoutingView/MediaRoutingView.cpp
	MediaRoutingView/MediaWire.cpp

	NodeManager/AddOnHost.cpp
	NodeManager/Connection.cpp
	NodeManager/NodeManager.cpp
	NodeManager/NodeGroup.cpp
	NodeManager/NodeRef.cpp
	NodeManager/NodeSyncThread.cpp

	ParameterView/ParameterWindowManager.cpp
	ParameterView/ParameterWindow.cpp
	ParameterView/ParameterContainerView.cpp

	Persistence/ExportContext.cpp
	Persistence/ImportContext.cpp
	Persistence/Importer.cpp
	Persistence/StringContent.cpp
	Persistence/XML.cpp
	Persistence/Wrappers/MediaFormatIO.cpp
	Persistence/Wrappers/MessageIO.cpp

	TipManager/TipManager.cpp
	TipManager/TipManagerImpl.cpp
	TipManager/TipWindow.cpp
	TipManager/TipView.cpp

	TransportView/TransportView.cpp
	TransportView/TransportWindow.cpp

	ValControl/ValControl.cpp
	ValControl/ValControlSegment.cpp
	ValControl/ValControlDigitSegment.cpp
	ValControl/ValCtrlLayoutEntry.cpp
	ValControl/NumericValControl.cpp
)

foreach(_t cortex_support cortex_views)
	target_include_directories(${_t} PRIVATE ${_cortex_dirs}
		${EXPAT_INCLUDE_DIRS})
	target_compile_options(${_t} PRIVATE -include LinuxBuildCompatibility.h)
	UseMediaKit(${_t})
endforeach()

Application(
	Cortex

	SOURCES
	RouteApp/ConnectionIO.cpp
	RouteApp/DormantNodeIO.cpp
	RouteApp/LiveNodeIO.cpp
	RouteApp/NodeSetIOContext.cpp
	RouteApp/RouteApp.cpp
	RouteApp/RouteAppNodeManager.cpp
	RouteApp/RouteWindow.cpp
	RouteApp/route_app_io.cpp
	RouteApp/StatusView.cpp

	LIBS cortex_views cortex_support media tracker translation localestub
		${EXPAT_LIBRARIES}
	INCLUDES ${_cortex_dirs} ${EXPAT_INCLUDE_DIRS}
	RDEF Resource.rdef
)
UseMediaKit(Cortex)

# CompileRdef writes its output next to the rdef's path in the build tree.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/AddOnHost)
Application(
	CortexAddOnHost

	SOURCES
	AddOnHost/AddOnHostApp.cpp

	LIBS cortex_support media localestub
	INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/support
	RDEF AddOnHost/AddOnHost_Resource.rdef
)
UseMediaKit(CortexAddOnHost)

DoCatalogs("x-vnd.Cortex.Route" apps/cortex/RouteApp)
DoCatalogs("x-vnd.Cortex.AddOnHost" apps/cortex/AddOnHost)
DoCatalogs("x-vnd.Cortex.DormantNodeView" apps/cortex/DormantNodeView)
DoCatalogs("x-vnd.Cortex.InfoView" apps/cortex/InfoView)
DoCatalogs("x-vnd.Cortex.MediaRoutingView" apps/cortex/MediaRoutingView)
DoCatalogs("x-vnd.Cortex.NodeManager" apps/cortex/NodeManager)
DoCatalogs("x-vnd.Cortex.ParameterView" apps/cortex/ParameterView)
DoCatalogs("x-vnd.Cortex.TipManager" apps/cortex/TipManager)
DoCatalogs("x-vnd.Cortex.TransportView" apps/cortex/TransportView)
DoCatalogs("x-vnd.Cortex.support" apps/cortex/support)

add_subdirectory(addons)
