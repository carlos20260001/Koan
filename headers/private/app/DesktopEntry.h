/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 * Desktop Entry Specification (desktop-entry-spec v1.5) implementation.
 */

#ifndef _DESKTOP_ENTRY_H
#define _DESKTOP_ENTRY_H

#include <String.h>
#include <map>


enum DesktopEntryType {
	DesktopEntryTypeUnknown = 0,
	DesktopEntryTypeApplication,
	DesktopEntryTypeLink,
	DesktopEntryTypeDirectory
};


class DesktopEntry {
public:
					DesktopEntry(const char* path);
					~DesktopEntry();

	bool			IsValid() const;
	DesktopEntryType	Type() const;
	const char*		Path() const;
	const char*		Name() const;
	const char*		Exec() const;
	const char*		Icon() const;
	bool			Hidden() const;
	bool			NoDisplay() const;
	bool			ShouldShow() const;

	const char*		GetString(const char* key, const char* defaultValue = "") const;

	// Resolves Icon= to an image file per the Icon Theme Specification
	// (hicolor theme, PNG, then /usr/share/pixmaps): an absolute Icon= is
	// used as is; otherwise the smallest size >= size, else the largest.
	status_t		FindIconPath(int32 size, BString* path) const;

private:
		void		Parse();
		void		SetValue(const char* key, const char* value);

	BString		fPath;
	DesktopEntryType	fType;
	bool		fValid;

	BString		fName;
	BString		fExec;
	BString		fIcon;
	BString		fTypeStr;
	BString		fCategories;
	BString		fOnlyShowIn;
	BString		fNotShowIn;
	bool		fHidden;
	bool		fNoDisplay;
	bool		fTerminal;
	bool		fStartupNotify;

	std::map<BString, BString> fValues;
};

#endif // _DESKTOP_ENTRY_H
