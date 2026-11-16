HaikuGpuAccelerant(rpi_display.accelerant
	SOURCES
		accelerant.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/rpi_display
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
	LIBS haiku_gpu_accelerantscommon
)
