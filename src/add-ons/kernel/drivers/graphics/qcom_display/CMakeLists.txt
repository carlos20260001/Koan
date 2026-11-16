HaikuGpuKernelAddon(qcom_display
	SOURCES
		display_port.cpp
		dp_tu.c
		dpu.cpp
		driver.cpp
		usbc_dp.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/drivers
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/qcom_display
	CXXFLAGS -fno-rtti
)
