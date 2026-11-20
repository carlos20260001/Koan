# print_addon_server: runs the printer driver add-ons for print_server.
Server(
	print_addon_server

	SOURCES
	PrintAddOnServerApplication.cpp

	LIBS printutils localestub
	RDEF print_addon_server.rdef
)

VosPrivateHeaders(print_addon_server shared print)
