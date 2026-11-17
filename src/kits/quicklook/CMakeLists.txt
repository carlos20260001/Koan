# air/OS's Quick Look client library (libquicklook.so): Tracker and other
# programs ask for a preview panel of a file.
AddOn(
	quicklook
	SHARED

	SOURCES
	QuickLook.cpp

	LIBS be root
)

UsePrivateHeaders(quicklook shared)
install(TARGETS quicklook DESTINATION /system/lib/)
