HaikuGpuKernelAddon(v3d
	SOURCES
		driver.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/drivers
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
	CXXFLAGS -fno-rtti
)
