add_subdirectory(engine)

HaikuGpuAccelerant(neomagic.accelerant
	SOURCES
		Acceleration.c
		Cursor.c
		EngineManagment.c
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
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/neomagic
		${CMAKE_CURRENT_SOURCE_DIR}/engine
	LIBS haiku_gpu_neomagic_engine
)

HaikuGpuDepends(neomagic.accelerant haiku_driver_neomagic)
