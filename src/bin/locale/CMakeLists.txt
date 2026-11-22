# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Catalog tools; collectcatkeys takes RegExp.cpp from the Tracker kit.
set(_tracker ${PROJECT_SOURCE_DIR}/src/kits/tracker)

Application(collectcatkeys
	SOURCES collectcatkeys.cpp ${_tracker}/RegExp.cpp
	LIBS localestub
)
# Tracker's RegExp.h, not the libshared one of the same name.
target_include_directories(collectcatkeys BEFORE PRIVATE ${_tracker})
Application(linkcatkeys SOURCES linkcatkeys.cpp)
Application(dumpcatalog SOURCES dumpcatalog.cpp)
# Debian's libc-bin owns /usr/bin/locale; this one is built, not installed.
Application(locale SOURCES locale.cpp)

foreach(_tool collectcatkeys linkcatkeys dumpcatalog locale)
	UsePrivateHeaders(${_tool} locale shared)
endforeach()
