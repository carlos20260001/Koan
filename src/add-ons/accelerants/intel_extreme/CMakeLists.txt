HaikuGpuAccelerant(intel_extreme.accelerant
	SOURCES
		accelerant.cpp
		cursor.cpp
		dpms.cpp
		engine.cpp
		hooks.cpp
		memory.cpp
		mode.cpp
		overlay.cpp
		# overlay_3d_i965.cpp
		pll.cpp
		# classes
		FlexibleDisplayInterface.cpp
		PanelFitter.cpp
		Ports.cpp
		Pipes.cpp
		TigerLakePLL.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/intel_extreme
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
		${HAIKU_GPU_PRIVATE_HEADERS}/shared
	LIBS be haiku_gpu_accelerantscommon
)
