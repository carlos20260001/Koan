# air/OS's print utilities (libprintutils.a): picture iteration and
# printing, spool jobs, transport and driver add-on glue, shared by the
# print_server, the printer add-ons and the Printers preferences.
# VitruvianOS prints through CUPS (src/libs/printcups); these stay for the
# air/OS printer drivers. PrintPanel, JobSetupPanel, Printer, PrinterRoster
# and Template were never built by Haiku's Jamfile either.
AddOn(
	printutils
	STATIC

	SOURCES
	BeUtils.cpp
	BeUtilsTranslation.cpp
	FolderWatcher.cpp
	Jobs.cpp
	PictureIterator.cpp
	PicturePrinter.cpp
	PrinterDriverAddOn.cpp
	PrintTransport.cpp
	PrintAddOnServer.cpp
	PrintAddOnServerProtocol.cpp
)

UsePrivateHeaders(printutils shared print)
