/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Desktop Entry Scanner
 * Scans .desktop files in application directories per desktop-entry-spec.
 */

#ifndef _DESKTOP_ENTRY_SCANNER_H
#define _DESKTOP_ENTRY_SCANNER_H

#include <String.h>
#include <ObjectList.h>
#include <StringList.h>

class DesktopEntry;

class DesktopEntryScanner {
public:
	DesktopEntryScanner(const char* applicationsPath = NULL);
	~DesktopEntryScanner();

	status_t	Scan();
	int32		CountEntries() const;
	DesktopEntry* EntryAt(int32 index) const;

private:
	status_t		_ScanDirectory(const char* path, const char* root,
						BStringList& seenIDs);

	BString		fAppsPath;
	BObjectList<DesktopEntry, true> fEntries;
};

#endif
