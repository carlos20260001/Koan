HaikuGpuKernelAddon(rpi_display
	SOURCES
		driver.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/drivers
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/rpi_display
	CXXFLAGS -fno-rtti
)
