# print_server: air/OS's print server (Be's print_server API: printers,
# transports, spooling) driving the printer add-ons through
# print_addon_server.
Server(
	print_server

	SOURCES
	PrintServerApp.cpp
	PrintServerApp.R5.cpp
	PrintServerApp.Scripting.cpp
	Printer.Scripting.cpp
	Printer.cpp
	Transport.Scripting.cpp
	Transport.cpp
	ResourceManager.cpp
	Settings.cpp
	ConfigWindow.cpp
	# AddFields(), from libprint (src/libs/print/libprint)
	${PROJECT_SOURCE_DIR}/src/libs/print/libprint/PrintUtils.cpp

	LIBS translation localestub printutils
	RDEF print_server.rdef
)

VosPrivateHeaders(print_server app shared print)
target_include_directories(print_server PRIVATE
	"${PROJECT_SOURCE_DIR}/headers/libs/print/libprint")
DoCatalogs("x-vnd.Be-PSRV" servers/print)
