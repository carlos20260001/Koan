HaikuGpuStaticLibrary(graphicscommon KERNEL
	SOURCES
		memory_manager.c
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/radeon
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
	CFLAGS -DENABLE_LOGGING -DLOG_INCLUDE_STARTUP
)
