# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Gutenprint driver on Debian's libgutenprint (GUTENPRINT in
# build/deps.cmake).
find_package(PkgConfig REQUIRED)
# GUTENPRINT_* is taken by build/deps.cmake.
pkg_check_modules(GUTENPRINT_PC REQUIRED gutenprint)

PrintDriverAddOn(print_gutenprint "Gutenprint"
	SOURCES
	GPBand.cpp
	GPBinding.cpp
	GPCapabilities.cpp
	GPCapabilityExtractor.cpp
	GPData.cpp
	GPDriver.cpp
	GPJob.cpp
	GPJobConfiguration.cpp
	GPParameterVisitor.cpp
	GPPrinterDriver.cpp
	SelectPrinterDialog.cpp

	LIBS ${GUTENPRINT_PC_LINK_LIBRARIES}

	RDEF Gutenprint.rdef
)
target_include_directories(print_gutenprint PRIVATE ${GUTENPRINT_PC_INCLUDE_DIRS})
target_compile_options(print_gutenprint PRIVATE ${GUTENPRINT_PC_CFLAGS_OTHER}
	-Wno-sign-compare)
