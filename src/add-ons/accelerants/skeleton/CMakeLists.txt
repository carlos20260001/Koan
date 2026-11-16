# Template for new accelerants; ../CMakeLists.txt does not add it (nor did
# the Jamfile).
add_subdirectory(engine)

HaikuGpuAccelerant(skel.accelerant
	SOURCES
		Acceleration.c
		Cursor.c
		EngineManagement.c
		GetAccelerantHook.c
		GetDeviceInfo.c
		GetModeInfo.c
		GetTimingConstraints.c
		InitAccelerant.c
		Overlay.c
		ProposeDisplayMode.c
		SetDisplayMode.c
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/skeleton
		${CMAKE_CURRENT_SOURCE_DIR}/engine
	LIBS haiku_gpu_skeleton_engine
)

HaikuGpuDepends(skel.accelerant haiku_driver_skel.driver)
