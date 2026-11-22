# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# The NPU of Ryzen AI processors through Linux's amdxdna DRM accel driver
# (/dev/accel/accel<N>); see README.
NeuralBackendAddOn(amdxdna.neural
	SOURCES
	Backend.cpp
	Bundle.cpp
	Device.cpp
	Program.cpp
	Sha256.cpp
)
# Moving tensors in and out of a design's layout is a loop over bytes.
target_compile_options(amdxdna.neural PRIVATE -O2)
