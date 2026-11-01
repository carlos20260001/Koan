/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Desktop Entry Scanner implementation
 */

#include "DesktopEntryScanner.h"
#include "DesktopEntry.h"

#include <dirent.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <StringList.h>


static void
append_path(BString& path, const char* component)
{
	if (path.Length() > 0 && path[path.Length() - 1] != '/')
		path << "/";
	path << component;
}


static BString
desktop_id_for_path(const char* path, const char* root)
{
	BString relative(path);
	relative.Remove(0, strlen(root));
	if (relative.StartsWith("/"))
		relative.Remove(0, 1);
	relative.ReplaceAll("/", "-");
	return relative;
}

DesktopEntryScanner::DesktopEntryScanner(const char* appsPath)
	:
	fAppsPath(appsPath ? appsPath : "")
{
}

DesktopEntryScanner::~DesktopEntryScanner()
{
}

status_t
DesktopEntryScanner::Scan()
{
	fEntries.MakeEmpty();
	BStringList seenIDs;
	if (fAppsPath.Length() > 0)
		return _ScanDirectory(fAppsPath.String(), fAppsPath.String(), seenIDs);

	status_t status = B_OK;
	BString applications;
	const char* dataHome = getenv("XDG_DATA_HOME");
	if (dataHome != NULL && dataHome[0] != '\0') {
		applications = dataHome;
	} else {
		const char* home = getenv("HOME");
		if (home == NULL || home[0] == '\0')
			return B_BAD_VALUE;
		applications = home;
		append_path(applications, ".local/share");
	}
	append_path(applications, "applications");
	status = _ScanDirectory(applications.String(), applications.String(),
		seenIDs);
	if (status == B_NO_MEMORY)
		return status;

	BString dataDirs(getenv("XDG_DATA_DIRS") != NULL
			? getenv("XDG_DATA_DIRS") : "/usr/local/share:/usr/share");
	int32 start = 0;
	while (start <= dataDirs.Length()) {
		int32 end = dataDirs.FindFirst(':', start);
		if (end < 0)
			end = dataDirs.Length();
		BString directory(dataDirs.String() + start, end - start);
		if (directory.Length() > 0) {
			append_path(directory, "applications");
			status = _ScanDirectory(directory.String(), directory.String(),
				seenIDs);
			if (status == B_NO_MEMORY)
				return status;
		}
		if (end == dataDirs.Length())
			break;
		start = end + 1;
	}
	return B_OK;
}

status_t
DesktopEntryScanner::_ScanDirectory(const char* path, const char* root,
	BStringList& seenIDs)
{
	DIR* directory = opendir(path);
	if (directory == NULL)
		return B_ENTRY_NOT_FOUND;

	status_t status = B_OK;
	struct dirent* item;
	while ((item = readdir(directory)) != NULL) {
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
			continue;

		BString child(path);
		append_path(child, item->d_name);
		struct stat info;
		if (lstat(child.String(), &info) != 0 || S_ISLNK(info.st_mode))
			continue;

		if (S_ISDIR(info.st_mode)) {
			status = _ScanDirectory(child.String(), root, seenIDs);
			if (status == B_NO_MEMORY)
				break;
			continue;
		}
		if (!S_ISREG(info.st_mode) || !child.EndsWith(".desktop"))
			continue;

		BString id = desktop_id_for_path(child.String(), root);
		if (seenIDs.HasString(id.String()))
			continue;
		seenIDs.Add(id);

		DesktopEntry* entry = new(std::nothrow) DesktopEntry(child.String());
		if (entry == NULL) {
			status = B_NO_MEMORY;
			break;
		}
		if (entry->ShouldShow()) {
			if (!fEntries.AddItem(entry)) {
				delete entry;
				status = B_NO_MEMORY;
				break;
			}
		} else {
			delete entry;
		}
	}
	closedir(directory);
	return status;
}

int32
DesktopEntryScanner::CountEntries() const
{
	return fEntries.CountItems();
}

DesktopEntry*
DesktopEntryScanner::EntryAt(int32 index) const
{
	if (index < 0 || index >= fEntries.CountItems())
		return NULL;
	return (DesktopEntry*)fEntries.ItemAt(index);
}
