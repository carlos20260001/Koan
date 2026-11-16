HaikuGpuAccelerant(ati.accelerant
	SOURCES
		accelerant.cpp
		cursor.cpp
		engine.cpp
		hooks.cpp
		mode.cpp
		overlay.cpp

		mach64_cursor.cpp
		mach64_dpms.cpp
		mach64_draw.cpp
		mach64_init.cpp
		mach64_mode.cpp
		mach64_overlay.cpp
		mach64_util.cpp

		rage128_cursor.cpp
		rage128_dpms.cpp
		rage128_draw.cpp
		rage128_init.cpp
		rage128_mode.cpp
		rage128_overlay.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/ati
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
	LIBS be haiku_gpu_accelerantscommon
)
