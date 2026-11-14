AddOn(KoiPopControlLook SHARED
	SOURCES KoiPopControlLook.cpp
	LIBS be
	RDEF KoiPopControlLook.rdef
)
# The menu label is the file name.
set_target_properties(KoiPopControlLook PROPERTIES PREFIX "" SUFFIX "")
UsePrivateHeaders(KoiPopControlLook app interface shared)
