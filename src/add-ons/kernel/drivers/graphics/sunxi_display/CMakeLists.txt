HaikuGpuKernelAddon(sunxi_display
	SOURCES
		display_pipe.cpp
		driver.cpp
		twi.cpp
		typec.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/drivers
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/sunxi_display
	CXXFLAGS -fno-rtti
)
