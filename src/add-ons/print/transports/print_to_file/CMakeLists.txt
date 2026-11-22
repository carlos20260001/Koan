# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# The name is checked by ResourceManager::NeedsLocking() in
# src/servers/print.
PrintAddOn(print_to_file "Print To File"
	SOURCES
	print_transport.cpp
	FileSelector.cpp

	LIBS tracker
)
