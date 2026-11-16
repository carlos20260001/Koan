HaikuGpuAccelerant(intel_810.accelerant
	SOURCES
		accelerant.cpp
		engine.cpp
		hooks.cpp
		mode.cpp

		i810_dpms.cpp
		i810_init.cpp
		i810_mode.cpp
		i810_watermark.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/intel_810
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
	LIBS be haiku_gpu_accelerantscommon
)
