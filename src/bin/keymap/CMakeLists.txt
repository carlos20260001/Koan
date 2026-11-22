# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The target keymap is the host keymap compiler (src/tools/keymap); this
# is the command line tool of the image, installed as /bin/keymap.
Application(keymap_cli
	SOURCES
	main.cpp
	Keymap.cpp

	LIBS shared
	RDEF keymap.rdef
)
set_target_properties(keymap_cli PROPERTIES OUTPUT_NAME keymap)
UsePrivateHeaders(keymap_cli shared storage)
