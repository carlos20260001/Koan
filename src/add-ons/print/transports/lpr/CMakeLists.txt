# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

PrintAddOn(print_lpr "LPR"
	SOURCES
	Lpr.cpp
	LprSetupDlg.cpp
	LprTransport.cpp
	LpsClient.cpp
	${PRINT_TRANSPORT_SHARED_SOURCES}

	LIBS libprint

	INCLUDES
	${PROJECT_SOURCE_DIR}/headers/libs/print/libprint
)
target_include_directories(print_lpr BEFORE PRIVATE
	${PROJECT_SOURCE_DIR}/headers/libs/print/libprint)
target_compile_options(print_lpr PRIVATE -Wno-format-overflow)
# -iquote: headers/os/net on the global include path has its own Socket.h.
target_compile_options(print_lpr PRIVATE "-iquote${PRINT_TRANSPORT_SHARED}")
