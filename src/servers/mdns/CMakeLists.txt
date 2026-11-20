# mdns_server: air/OS's multicast DNS responder.
Server(
	mdns_server

	SOURCES
	MDNSServer.cpp

	RDEF mdns_server.rdef
)

VosPrivateHeaders(mdns_server app net shared)
