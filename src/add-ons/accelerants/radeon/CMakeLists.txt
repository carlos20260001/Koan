# libradeon.a is built by the kernel driver (drivers/graphics/radeon).
HaikuGpuAccelerant(radeon.accelerant
	SOURCES
		Acceleration.c
		CP.c
		Cursor.c
		EngineManagment.c
		GetAccelerantHook.c
		GetModeInfo.c
		GlobalData.c
		InitAccelerant.c
		ProposeDisplayMode.c
		SetDisplayMode.c
		crtc.c
		dpms.c
		driver_wrapper.c
		flat_panel.c
		impactv.c
		internal_tv_out.c
		monitor_detection.c
		monitor_routing.c
		multimon.c
		overlay.c
		overlay_management.c
		palette.c
		pll.c
		settings.cpp
		theatre_out.c
		vesa_modes.c
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/radeon
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
	LIBS haiku_gpu_accelerantscommon haiku_gpu_radeon be
)

HaikuGpuDepends(radeon.accelerant haiku_driver_radeon)
