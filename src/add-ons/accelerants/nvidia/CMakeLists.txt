add_subdirectory(engine)

HaikuGpuAccelerant(nvidia.accelerant
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
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/nvidia
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
		${CMAKE_CURRENT_SOURCE_DIR}/engine
	LIBS haiku_gpu_nvidia_engine haiku_gpu_accelerantscommon
)

HaikuGpuDepends(nvidia.accelerant haiku_driver_nvidia)
