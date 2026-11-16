# kernel_cpp.cpp came from src/system/kernel/util, deleted with the Haiku
# kernel (M6); HaikuGpuKernelAddon reports it and leaves it out.
HaikuGpuKernelAddon(intel_extreme
	SOURCES
		bios.cpp
		driver.cpp
		device.cpp
		intel_extreme.cpp
		power.cpp

		kernel_cpp.cpp
	SEARCH
		${PROJECT_SOURCE_DIR}/src/system/kernel/util
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/intel_extreme
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/vesa
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
	LIBS haiku_gpu_graphicscommon
)
