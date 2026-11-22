# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# PDF, XPS, EPUB and the other documents MuPDF opens. Debian's mupdf.pc
# is unusable (its "prefix" line is misspelt), so the library is named
# directly; the headers are in the chroot's include directory.
QuickLookAddOn(PDFPreview
	SOURCES
	PDFPreview.cpp
)
target_link_libraries(PDFPreview PRIVATE mupdf)
DoCatalogs("x-vnd.vos-QuickLook-PDFPreview" add-ons/quick_look/pdf)
