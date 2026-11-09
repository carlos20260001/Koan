/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 * Desktop Entry Specification (desktop-entry-spec v1.5) implementation.
 */

#include "DesktopEntry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <StringList.h>

static bool
desktop_list_contains(const char* list, const char* desktop)
{
	if (list == NULL || desktop == NULL || *desktop == '\0')
		return false;

	const char* entry = list;
	while (*entry != '\0') {
		const char* end = strchr(entry, ';');
		if (end == NULL)
			end = entry + strlen(entry);
		if ((size_t)(end - entry) == strlen(desktop)
			&& strncmp(entry, desktop, end - entry) == 0) {
			return true;
		}
		entry = *end == ';' ? end + 1 : end;
	}
	return false;
}

DesktopEntry::DesktopEntry(const char* path)
	:
	fPath(path ? path : ""),
	fType(DesktopEntryTypeUnknown),
	fValid(false),
	fHidden(false),
	fNoDisplay(false),
	fTerminal(false),
	fStartupNotify(false)
{
	Parse();
}

DesktopEntry::~DesktopEntry()
{
}

void
DesktopEntry::Parse()
{
	FILE* file = fopen(fPath.String(), "r");
	if (file == NULL)
		return;

	char line[1024];
	bool inDesktopEntry = false;

	while (fgets(line, sizeof(line), file) != NULL) {
		char* p = line;
		// Skip whitespace
		while (*p == ' ' || *p == '\t')
			p++;

		// Skip comments and empty lines
		if (*p == '#' || *p == '\0' || *p == '\n')
			continue;

		// Check for [Desktop Entry] group
		if (*p == '[') {
			char* end = strchr(p, ']');
			if (end != NULL) {
				*end = '\0';
				if (strcmp(p + 1, "Desktop Entry") == 0)
					inDesktopEntry = true;
				else
					inDesktopEntry = false;
			}
			continue;
		}

		if (!inDesktopEntry)
			continue;

		// Parse key=value
		char* eq = strchr(p, '=');
		if (eq == NULL)
			continue;

		*eq = '\0';
		char* key = p;
		char* value = eq + 1;

		// Trim whitespace from key
		char* keyEnd = eq;
		while (keyEnd > key
			&& (keyEnd[-1] == ' ' || keyEnd[-1] == '\t')) {
			*--keyEnd = '\0';
		}

		// Trim whitespace from value
		char* valStart = value;
		while (*valStart == ' ' || *valStart == '\t')
			valStart++;

		char* valEnd = valStart + strlen(valStart);
		while (valEnd > valStart
			&& (valEnd[-1] == ' ' || valEnd[-1] == '\t'
				|| valEnd[-1] == '\r' || valEnd[-1] == '\n')) {
			*--valEnd = '\0';
		}

		SetValue(key, valStart);
	}

	fclose(file);

	// Determine Type
	const char* typeStr = GetString("Type", "");
	if (strcmp(typeStr, "Application") == 0)
		fType = DesktopEntryTypeApplication;
	else if (strcmp(typeStr, "Link") == 0)
		fType = DesktopEntryTypeLink;
	else if (strcmp(typeStr, "Directory") == 0)
		fType = DesktopEntryTypeDirectory;
	else
		fType = DesktopEntryTypeUnknown;

	fValid = fType != DesktopEntryTypeUnknown
		&& (fType != DesktopEntryTypeApplication || fExec.Length() > 0);
}

void
DesktopEntry::SetValue(const char* key, const char* value)
{
	if (strcmp(key, "Name") == 0)
		fName = value ? value : "";
	else if (strcmp(key, "Exec") == 0)
		fExec = value ? value : "";
	else if (strcmp(key, "Icon") == 0)
		fIcon = value ? value : "";
	else if (strcmp(key, "Type") == 0)
		fTypeStr = value ? value : "";
	else if (strcmp(key, "Categories") == 0)
		fCategories = value ? value : "";
	else if (strcmp(key, "OnlyShowIn") == 0)
		fOnlyShowIn = value ? value : "";
	else if (strcmp(key, "NotShowIn") == 0)
		fNotShowIn = value ? value : "";
	else if (strcmp(key, "Hidden") == 0)
		fHidden = (strcmp(value, "true") == 0);
	else if (strcmp(key, "NoDisplay") == 0)
		fNoDisplay = (strcmp(value, "true") == 0);
	else if (strcmp(key, "Terminal") == 0)
		fTerminal = (strcmp(value, "true") == 0);
	else if (strcmp(key, "StartupNotify") == 0)
		fStartupNotify = (strcmp(value, "true") == 0);

	fValues[key] = value ? value : "";
}

const char*
DesktopEntry::GetString(const char* key, const char* defaultValue) const
{
	std::map<BString, BString>::const_iterator it = fValues.find(key);
	if (it != fValues.end())
		return (*it).second.String();
	return defaultValue;
}

DesktopEntryType
DesktopEntry::Type() const
{
	return fType;
}

bool
DesktopEntry::IsValid() const
{
	return fValid;
}

const char*
DesktopEntry::Path() const
{
	return fPath.String();
}

const char*
DesktopEntry::Name() const
{
	return fName.String();
}

const char*
DesktopEntry::Exec() const
{
	return fExec.String();
}

const char*
DesktopEntry::Icon() const
{
	return fIcon.String();
}

bool
DesktopEntry::Hidden() const
{
	return fHidden;
}

bool
DesktopEntry::NoDisplay() const
{
	return fNoDisplay;
}

bool
DesktopEntry::ShouldShow() const
{
	if (!fValid || fHidden || fNoDisplay)
		return false;
	if (fType != DesktopEntryTypeApplication)
		return false;

	const char* currentDesktop = getenv("XDG_CURRENT_DESKTOP");
	if (fOnlyShowIn.Length() > 0) {
		bool matches = false;
		if (currentDesktop != NULL) {
			const char* desktop = currentDesktop;
			while (*desktop != '\0') {
				const char* end = strchr(desktop, ':');
				if (end == NULL)
					end = desktop + strlen(desktop);
				BString name(desktop, (int32)(end - desktop));
				if (desktop_list_contains(fOnlyShowIn.String(), name.String())) {
					matches = true;
					break;
				}
				desktop = *end == ':' ? end + 1 : end;
			}
		}
		if (!matches)
			return false;
	}
	if (currentDesktop != NULL) {
		const char* desktop = currentDesktop;
		while (*desktop != '\0') {
			const char* end = strchr(desktop, ':');
			if (end == NULL)
				end = desktop + strlen(desktop);
			BString name(desktop, (int32)(end - desktop));
			if (desktop_list_contains(fNotShowIn.String(), name.String()))
				return false;
			desktop = *end == ':' ? end + 1 : end;
		}
	}
	return true;
}


// Icon Theme Specification lookup, limited to what the desktop needs: the
// hicolor fallback theme in PNG and the legacy pixmaps directory.
static void
icon_base_directories(BStringList& bases)
{
	const char* dataHome = getenv("XDG_DATA_HOME");
	const char* home = getenv("HOME");
	if (dataHome != NULL && *dataHome != '\0')
		bases.Add(BString(dataHome) << "/icons");
	else if (home != NULL && *home != '\0')
		bases.Add(BString(home) << "/.local/share/icons");
	if (home != NULL && *home != '\0')
		bases.Add(BString(home) << "/.icons");

	BString dataDirs(getenv("XDG_DATA_DIRS") != NULL
		? getenv("XDG_DATA_DIRS") : "/usr/local/share:/usr/share");
	int32 start = 0;
	while (start <= dataDirs.Length()) {
		int32 end = dataDirs.FindFirst(':', start);
		if (end < 0)
			end = dataDirs.Length();
		if (end > start) {
			BString dir;
			dataDirs.CopyInto(dir, start, end - start);
			bases.Add(dir << "/icons");
		}
		start = end + 1;
	}
}


status_t
DesktopEntry::FindIconPath(int32 size, BString* path) const
{
	if (path == NULL)
		return B_BAD_VALUE;
	const char* icon = fIcon.String();
	if (*icon == '\0')
		return B_ENTRY_NOT_FOUND;

	if (icon[0] == '/') {
		path->SetTo(icon);
		return access(icon, R_OK) == 0 ? B_OK : B_ENTRY_NOT_FOUND;
	}

	static const int32 kSizes[] = { 16, 22, 24, 32, 48, 64, 96, 128, 256 };
	const int32 sizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

	// Try sizes from the best fit upwards, then the smaller ones downwards.
	int32 first = 0;
	while (first < sizeCount && kSizes[first] < size)
		first++;
	int32 order[sizeof(kSizes) / sizeof(kSizes[0])];
	int32 count = 0;
	for (int32 i = first; i < sizeCount; i++)
		order[count++] = kSizes[i];
	for (int32 i = first - 1; i >= 0; i--)
		order[count++] = kSizes[i];

	BStringList bases;
	icon_base_directories(bases);
	for (int32 i = 0; i < count; i++) {
		for (int32 b = 0; b < bases.CountStrings(); b++) {
			BString candidate(bases.StringAt(b));
			candidate << "/hicolor/" << order[i] << "x" << order[i]
				<< "/apps/" << icon << ".png";
			if (access(candidate.String(), R_OK) == 0) {
				*path = candidate;
				return B_OK;
			}
		}
	}

	BString pixmap("/usr/share/pixmaps/");
	pixmap << icon << ".png";
	if (access(pixmap.String(), R_OK) == 0) {
		*path = pixmap;
		return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}
