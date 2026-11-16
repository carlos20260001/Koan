add_subdirectory(engine)

HaikuGpuAccelerant(matrox.accelerant
	SOURCES
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
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/matrox
		${CMAKE_CURRENT_SOURCE_DIR}/engine
	LIBS haiku_gpu_matrox_engine
)

HaikuGpuDepends(matrox.accelerant haiku_driver_matrox)
