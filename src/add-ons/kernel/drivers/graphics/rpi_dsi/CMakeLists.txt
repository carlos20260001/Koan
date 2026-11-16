HaikuGpuKernelAddon(rpi_dsi
	SOURCES
		driver.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/drivers
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/rpi_dsi
	CXXFLAGS -fno-rtti
)
