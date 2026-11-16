HaikuGpuAccelerant(s3.accelerant
	SOURCES
		accel.cpp
		cursor.cpp
		engine.cpp
		hooks.cpp
		mode.cpp
		register_io.cpp

		savage_cursor.cpp
		savage_dpms.cpp
		savage_draw.cpp
		savage_edid.cpp
		savage_init.cpp
		savage_mode.cpp

		trio64_cursor.cpp
		trio64_dpms.cpp
		trio64_draw.cpp
		trio64_init.cpp
		trio64_mode.cpp

		virge_cursor.cpp
		virge_dpms.cpp
		virge_draw.cpp
		virge_edid.cpp
		virge_init.cpp
		virge_mode.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/s3
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
	LIBS be haiku_gpu_accelerantscommon
)
