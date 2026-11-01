/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _DESKTOP_ENTRY_LAUNCHER_H
#define _DESKTOP_ENTRY_LAUNCHER_H

#include <Entry.h>
#include <SupportDefs.h>

class DesktopEntry;

status_t LaunchDesktopEntry(const DesktopEntry& entry,
	const entry_ref* refs = NULL, int32 refCount = 0);

#endif
