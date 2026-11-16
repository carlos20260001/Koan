HaikuGpuAccelerant(rk3588_display.accelerant
	SOURCES
		accelerant.cpp
		cursor.cpp
		hooks.cpp
		layout.cpp
		mode.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
		${HAIKU_GPU_PRIVATE_HEADERS}/shared
		${PROJECT_SOURCE_DIR}/src/add-ons/kernel/drivers/graphics/rk3588_display
	LIBS haiku_gpu_accelerantscommon
)
