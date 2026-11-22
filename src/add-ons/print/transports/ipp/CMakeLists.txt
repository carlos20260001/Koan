# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

PrintAddOn(print_ipp "IPP"
	SOURCES
	Ipp.cpp
	IppSetupDlg.cpp
	IppTransport.cpp
	HttpURLConnection.cpp
	IppContent.cpp
	IppURLConnection.cpp
	${PRINT_TRANSPORT_SHARED_SOURCES}

	LIBS shared

)
UsePrivateHeaders(print_ipp shared print)
target_compile_options(print_ipp PRIVATE -Wno-format-overflow)
# -iquote: headers/os/net on the global include path has its own Socket.h.
target_compile_options(print_ipp PRIVATE "-iquote${PRINT_TRANSPORT_SHARED}")
