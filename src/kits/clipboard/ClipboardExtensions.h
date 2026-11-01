/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Clipboard Extensions (clipboard-extensions-spec Phase 4 / M2 milestone)
 */

#ifndef _CLIPBOARD_EXTENSIONS_H
#define _CLIPBOARD_EXTENSIONS_H

#include <String.h>

class ClipboardExtensions {
public:
	ClipboardExtensions();
	~ClipboardExtensions();

	status_t RegisterFormat(const char* mimeType);
	status_t UnregisterFormat(const char* mimeType);
};

#endif
