# libneural: air/OS's Neural Kit (tensors, TFLite models, sessions, the CPU
# operators and speech recognition). Hardware back ends are add-ons
# (src/add-ons/neural) found through NeuralRoster.
AddOn(neural
	SHARED

	SOURCES
	GraphData.cpp
	Interpreter.cpp
	NeuralModel.cpp
	NeuralRoster.cpp
	NeuralSession.cpp
	NeuralUtils.cpp
	Tensor.cpp
	TFLiteLoader.cpp

	# speech recognition: the core, which the host tests build too, and the
	# class applications use
	SpeechAudio.cpp
	SpeechEngine.cpp
	SpeechModel.cpp
	SpeechWhisper.cpp
	SpeechRecognizer.cpp

	# ops: the CPU kernels, one file per family of operators
	ops/Activation.cpp
	ops/Arithmetic.cpp
	ops/Convolution.cpp
	ops/Detection.cpp
	ops/Gather.cpp
	ops/Kernels.cpp
	ops/MatMul.cpp
	ops/Pooling.cpp
	ops/Quantize.cpp
	ops/Reduce.cpp
	ops/Resize.cpp
	ops/Shape.cpp

	LIBS be root
)

UsePrivateHeaders(neural shared storage)
# SpeechRecognizer.h uses the classic Media Kit's <MediaDefs.h>.
target_include_directories(neural BEFORE PRIVATE
	${CMAKE_SOURCE_DIR}/headers/os/media
)
target_include_directories(neural PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/neural
	${CMAKE_CURRENT_SOURCE_DIR}/ops
)
# The kernels are loops over numbers; they are written for the vectorizer,
# which -O2 holds back.
target_compile_options(neural PRIVATE -O3)
