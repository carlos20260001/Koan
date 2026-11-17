# Neural Kit tests. neural_kit_test (tensors, models, sessions, a back end
# of its own) and speech_test (the recognizer's parts without a model) need
# no hardware: CTest runs them. The back end tests need their device
# (Vulkan GPU, AMD XDNA NPU, RK3588 NPU) and are only built.

Test(neural_kit_test
	SOURCES neural_kit_test.cpp
	LIBS neural
)

Test(speech_test
	SOURCES speech_test.cpp
	LIBS neural
)

Test(vulkan_backend_test
	SOURCES vulkan_backend_test.cpp
	LIBS neural
)

set(_vos_neural_tests neural_kit_test speech_test vulkan_backend_test)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
	Test(amdxdna_backend_test
		SOURCES amdxdna_backend_test.cpp
		LIBS neural
	)
	list(APPEND _vos_neural_tests amdxdna_backend_test)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
	Test(rk3588_npu_backend_test
		SOURCES rk3588_npu_backend_test.cpp
		LIBS neural translation
	)
	list(APPEND _vos_neural_tests rk3588_npu_backend_test)
endif()

foreach(_test ${_vos_neural_tests})
	target_include_directories(${_test} PRIVATE
		${CMAKE_SOURCE_DIR}/headers/private/neural)
	# NeuralKit.h -> SpeechRecognizer.h uses the classic <MediaDefs.h>.
	target_include_directories(${_test} BEFORE PRIVATE
		${CMAKE_SOURCE_DIR}/headers/os/media)
endforeach()

enable_testing()
add_test(NAME neural_kit_test COMMAND neural_kit_test)
add_test(NAME speech_test COMMAND speech_test)
