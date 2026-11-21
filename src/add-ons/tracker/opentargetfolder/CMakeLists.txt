# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# An application and an add-on in one file (it has main()).
RunnableAddOn(
	OpenTargetFolder

	SOURCES
	opentargetfolder.cpp

	LIBS tracker

	RDEF opentargetfolder.rdef
)

set_target_properties(OpenTargetFolder PROPERTIES OUTPUT_NAME "Open Target Folder")
UsePrivateHeaders(OpenTargetFolder tracker)
