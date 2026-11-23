# Hardware acceleration kit: the overclock backends of the Hardware preflet.
# The OpenGL renderer is chosen by Mesa hgl's renderer roster (libGL-hgl,
# src/add-ons/opengl), so the kit has no GL renderer chooser.
AddOn(hwaccel
	SHARED

	SOURCES
	Overclock.cpp

	LIBS be
)
