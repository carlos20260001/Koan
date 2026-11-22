# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Plain text, and CSV/TSV as a table (BColumnListView, in libbe here).
QuickLookAddOn(TextPreview
	SOURCES
	TextPreview.cpp
	TablePreview.cpp
)
DoCatalogs("x-vnd.vos-QuickLook-TextPreview" add-ons/quick_look/text)
