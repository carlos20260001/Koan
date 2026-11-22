# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Any graphics processor with a Vulkan driver. The add-on links against no
# Vulkan library: it opens the loader (libvulkan1) at run time, with the
# Khronos headers in vulkan/, and its shaders are compiled ahead of time
# (shaders/compile.sh), so the build needs neither.
NeuralBackendAddOn(vulkan.neural
	SOURCES
	VulkanBackend.cpp
	VulkanDevice.cpp
	VulkanLibrary.cpp
	VulkanPlan.cpp
	VulkanProgram.cpp
)
target_include_directories(vulkan.neural PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
target_link_libraries(vulkan.neural PRIVATE ${CMAKE_DL_LIBS})
