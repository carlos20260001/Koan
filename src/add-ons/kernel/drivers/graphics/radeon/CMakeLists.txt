HaikuGpuStaticLibrary(radeon KERNEL
	SOURCES
		pll_access.c
		utils.c
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/radeon
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
)

HaikuGpuKernelAddon(radeon
	SOURCES
		driver.c
		agp.c
		bios.c
		detect.c
		DMA.c
		global_data.c
		init.c
		irq.c
		mem_controller.c
		CP_setup.c
		PCI_GART.c
		vip.c
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/radeon
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
	LIBS haiku_gpu_graphicscommon haiku_gpu_radeon
)
