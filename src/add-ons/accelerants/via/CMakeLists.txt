add_subdirectory(engine)

HaikuGpuAccelerant(via.accelerant
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
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/via
		${CMAKE_CURRENT_SOURCE_DIR}/engine
	LIBS haiku_gpu_via_engine
)

HaikuGpuDepends(via.accelerant haiku_driver_via)
