# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# Host tests of the KMS backend (no hardware), and the harness that drives
# the backend on a real or virtual KMS device (QEMU: vkms, virtio-gpu).

add_executable(kms_unit_test kms_unit_test.cpp)
target_link_libraries(kms_unit_test PRIVATE kms_core)
target_compile_options(kms_unit_test PRIVATE
	-include "${VOS_ROOT}/headers/build/LinuxBuildCompatibility.h")
add_test(NAME kms_unit_test COMMAND kms_unit_test)
