# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Neural Kit back ends, one per kind of hardware, found by NeuralRoster in
# add-ons/neural as <name>.neural (also the target name), <name> being
# what BNeuralBackend::Name() returns.
macro(NeuralBackendAddOn name)
	AddOn(${name} SHARED ${ARGN})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	target_link_libraries(${name} PRIVATE neural be root)
	target_link_options(${name} PRIVATE "-Wl,--no-undefined")
	target_include_directories(${name} PRIVATE
		${PROJECT_SOURCE_DIR}/headers/os/add-ons/neural)
endmacro()

# rk3588_npu: left out; it drives air/OS's RK3588 NPU kernel driver, and
# Linux's driver for that NPU (Rocket, DRM accel) has another interface
# (PORTING.md M9, add-ons wave 3).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
	add_subdirectory(amdxdna)
endif()
add_subdirectory(vulkan)

# CTest: what NeuralRoster looks up in each back end.
enable_testing()
set(_neural_backends $<TARGET_FILE:vulkan.neural>)
if(TARGET amdxdna.neural)
	list(APPEND _neural_backends $<TARGET_FILE:amdxdna.neural>)
endif()
add_test(NAME neural_backends_entry_points
	COMMAND sh "${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh" "${CMAKE_OBJDUMP}"
		instantiate_neural_backend
		-- ${_neural_backends})
