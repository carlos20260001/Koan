HaikuGpuAccelerant(et6x00.accelerant
	SOURCES
		Acceleration.c
		EngineManagment.c
		GetAccelerantHook.c
		GetModeInfo.c
		GlobalData.c
		InitAccelerant.c
		ProposeDisplayMode.c
		SetDisplayMode.c
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/et6x00
)

HaikuGpuDepends(et6x00.accelerant haiku_driver_et6x00.driver)
