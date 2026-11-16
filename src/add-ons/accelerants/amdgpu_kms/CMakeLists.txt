HaikuGpuAccelerant(amdgpu_kms.accelerant
	SOURCES
		accelerant.cpp
		cursor.cpp
		device_copy.cpp
		hooks.cpp
		kms.cpp
		layout.cpp
		mode.cpp
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/amdgpu_drm
		${HAIKU_GPU_PRIVATE_HEADERS}/shared
	LIBS haiku_gpu_accelerantscommon
)

# checks and times the copies by the GPU beside app_server (not in the image)
add_executable(amdcopytest
	copy_test.cpp
	device_copy.cpp
	kms.cpp
)
target_compile_options(amdcopytest PRIVATE -include LinuxBuildCompatibility.h)
target_include_directories(amdcopytest PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${HAIKU_GPU_PRIVATE_HEADERS}
	${HAIKU_GPU_PRIVATE_HEADERS}/graphics
	${HAIKU_GPU_PRIVATE_HEADERS}/graphics/common
	${HAIKU_GPU_PRIVATE_HEADERS}/graphics/amdgpu_drm
	${HAIKU_GPU_PRIVATE_HEADERS}/shared
)
# Haiku links every program against libroot implicitly.
target_link_libraries(amdcopytest PRIVATE root)
