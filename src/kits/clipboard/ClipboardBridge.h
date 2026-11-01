/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Clipboard Bridge (clipboard-spec Phase 3 / M2 milestone)
 */

#ifndef _CLIPBOARD_BRIDGE_H
#define _CLIPBOARD_BRIDGE_H

#include <String.h>

class ClipboardBridge {
public:
	ClipboardBridge();
	~ClipboardBridge();

	status_t Copy(const char* text);
	status_t Paste(BString& out);
};

#endif
