# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# The KMS backend without app_server: kms_core (the device, layouts,
# buffers, flips, GPU pass), the host tests (no hardware), and kms_harness,
# which drives the backend on a real or virtual KMS device (QEMU: vkms,
# virtio-gpu); kms_harness is not a CTest test, it needs a DRM device and
# its master.

add_library(kms_core STATIC
	${KMS_CORE_SOURCES}
	"${KMS_DIR}/../SplashHandoff.cpp"
	"${VOS_ROOT}/src/add-ons/accelerants/common/decode_edid.c"
)
target_include_directories(kms_core PUBLIC
	"${KMS_DIR}"
	"${KMS_DIR}/.."
	"${VOS_ROOT}/headers/private/graphics/common"
	"${VOS_ROOT}/headers/private"
	"${VOS_ROOT}/headers/os/add-ons/graphics")
target_compile_definitions(kms_core PUBLIC ${KMS_DEFINITIONS})
target_link_libraries(kms_core PUBLIC ${KMS_LIBS} pthread)
target_compile_options(kms_core PRIVATE
	-include "${VOS_ROOT}/headers/build/LinuxBuildCompatibility.h")

add_executable(kms_unit_test kms_unit_test.cpp)
target_link_libraries(kms_unit_test PRIVATE kms_core)
target_compile_options(kms_unit_test PRIVATE
	-include "${VOS_ROOT}/headers/build/LinuxBuildCompatibility.h")
add_test(NAME kms_unit_test COMMAND kms_unit_test)

add_executable(kms_harness kms_harness.cpp)
target_link_libraries(kms_harness PRIVATE kms_core)
target_compile_options(kms_harness PRIVATE
	-include "${VOS_ROOT}/headers/build/LinuxBuildCompatibility.h")
