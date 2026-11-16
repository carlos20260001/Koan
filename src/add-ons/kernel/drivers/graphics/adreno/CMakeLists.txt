# qcom_scm.cpp lived in drivers/common/qcom, deleted with the other Haiku
# kernel add-ons (M6); HaikuGpuKernelAddon reports it and leaves it out.
HaikuGpuKernelAddon(adreno
	SOURCES
		adreno.cpp
		adreno_drm.cpp
		qcom_scm.cpp
		fdt.c
		fdt_ro.c
	SEARCH
		${PROJECT_SOURCE_DIR}/src/add-ons/kernel/drivers/common/qcom
		${PROJECT_SOURCE_DIR}/src/libs/libfdt
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${PROJECT_SOURCE_DIR}/headers/libs/libfdt
		${PROJECT_SOURCE_DIR}/src/add-ons/kernel/drivers/common/qcom
)
