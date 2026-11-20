# location_server: air/OS's location service (LOCATION_GET_POSITION). On
# Linux it asks GeoClue 2 first (GeoClue.cpp, over sd-bus) and falls back to
# its own BeaconDB lookup.
Server(
	location_server

	SOURCES
	LocationServer.cpp
	GeoClue.cpp

	LIBS shared systemd
	RDEF location_server.rdef
)

VosPrivateHeaders(location_server app net netservices shared)
