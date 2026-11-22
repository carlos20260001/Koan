# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Transport add-ons. The device transports open the Linux printer nodes:
# /dev/lp* (parallel), /dev/usb/lp* (usblp) and the serial ttys.
# Socket, SocketStream and DbgMsg (shared/) are compiled into IPP and LPR.
set(PRINT_TRANSPORT_SHARED ${CMAKE_CURRENT_SOURCE_DIR}/shared)
set(PRINT_TRANSPORT_SHARED_SOURCES
	${PRINT_TRANSPORT_SHARED}/DbgMsg.cpp
	${PRINT_TRANSPORT_SHARED}/Socket.cpp
	${PRINT_TRANSPORT_SHARED}/SocketStream.cpp
)
# The device transports share the kit's init_transport()/exit_transport().
set(PRINT_TRANSPORT_ADDON_SOURCE
	${PROJECT_SOURCE_DIR}/src/kits/print/PrintTransportAddOn.cpp)

add_subdirectory(hp_jetdirect)
add_subdirectory(ipp)
add_subdirectory(lpr)
add_subdirectory(parallel_port)
add_subdirectory(print_to_file)
add_subdirectory(serial_port)
add_subdirectory(usb_port)
