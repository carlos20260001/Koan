HaikuGpuAccelerant(3dfx.accelerant
	SOURCES
		accelerant.cpp
		cursor.cpp
		engine.cpp
		hooks.cpp
		mode.cpp
		overlay.cpp

		3dfx_cursor.cpp
		3dfx_dpms.cpp
		3dfx_draw.cpp
		3dfx_edid.cpp
		3dfx_init.cpp
		3dfx_mode.cpp
		3dfx_overlay.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/3dfx
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
	LIBS be haiku_gpu_accelerantscommon
)
