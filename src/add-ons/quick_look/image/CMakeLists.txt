# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Images through the Translation Kit.
QuickLookAddOn(ImagePreview
	SOURCES
	ImagePreview.cpp
)
target_link_libraries(ImagePreview PRIVATE translation)
DoCatalogs("x-vnd.vos-QuickLook-ImagePreview" add-ons/quick_look/image)
