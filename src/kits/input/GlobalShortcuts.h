/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Global Shortcuts (global-shortcuts-spec Phase 4 / M4 milestone)
 */

#ifndef _GLOBAL_SHORTCUTS_H
#define _GLOBAL_SHORTCUTS_H

#include <String.h>

class GlobalShortcuts {
public:
	GlobalShortcuts();
	~GlobalShortcuts();

	status_t RegisterShortcut(const char* shortcutId, const char* keyCombo);
};

#endif
