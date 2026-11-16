HaikuGpuKernelAddon(radeon_hd
	SOURCES
		driver.cpp
		device.cpp
		radeon_hd.cpp
		sensors.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/radeon_hd
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/kernel
		${HAIKU_GPU_PRIVATE_HEADERS}/shared
	LIBS haiku_gpu_graphicscommon
)
