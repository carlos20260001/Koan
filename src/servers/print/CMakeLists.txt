# print_server: air/OS's print server (Be's print_server API: printers,
# transports, spooling) driving the printer add-ons through
# print_addon_server. On VitruvianOS the printers are CUPS queues:
# CupsPrinters.cpp keeps a spool folder (driver "CUPS") for each queue and
# hands their jobs, page and job setup to libprintcups.
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
	CupsPrinters.cpp
	# AddFields(), from libprint (src/libs/print/libprint)
	${PROJECT_SOURCE_DIR}/src/libs/print/libprint/PrintUtils.cpp

	LIBS translation localestub printutils printcups
	RDEF print_server.rdef
)

VosPrivateHeaders(print_server app shared print)
target_include_directories(print_server PRIVATE
	"${PROJECT_SOURCE_DIR}/headers/libs/print/libprint")
DoCatalogs("x-vnd.Be-PSRV" servers/print)
