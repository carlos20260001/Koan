HaikuGpuAccelerant(vesa.accelerant
	SOURCES
		accelerant.cpp
		dpms.cpp
		hooks.cpp
		mode.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/vesa
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
		${HAIKU_GPU_PRIVATE_HEADERS}/shared
	LIBS haiku_gpu_accelerantscommon
)
