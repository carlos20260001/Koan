/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <MimeAppsParser.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include <StringList.h>

static void
append_path(BString& path, const char* component)
{
	if (path.Length() > 0 && path[path.Length() - 1] != '/')
		path << "/";
	path << component;
}


static void
add_directory_list(BStringList& paths, const char* value,
	const char* suffix = NULL)
{
	if (value == NULL || *value == '\0')
		return;

	BString list(value);
	int32 start = 0;
	while (start <= list.Length()) {
		int32 end = list.FindFirst(':', start);
		if (end < 0)
			end = list.Length();
		BString path(list.String() + start, end - start);
		if (path.Length() > 0) {
			if (suffix != NULL)
				append_path(path, suffix);
			paths.Add(path);
		}
		if (end == list.Length())
			break;
		start = end + 1;
	}
}


static void
find_desktop_file(const char* directory, const char* root,
	const char* desktopID, BString* result)
{
	DIR* dir = opendir(directory);
	if (dir == NULL)
		return;

	struct dirent* item;
	while (result->Length() == 0 && (item = readdir(dir)) != NULL) {
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
			continue;
		BString path(directory);
		append_path(path, item->d_name);
		struct stat info;
		if (lstat(path.String(), &info) != 0 || S_ISLNK(info.st_mode))
			continue;
		if (S_ISDIR(info.st_mode)) {
			find_desktop_file(path.String(), root, desktopID, result);
		} else if (S_ISREG(info.st_mode) && path.EndsWith(".desktop")) {
			BString relative(path);
			relative.Remove(0, strlen(root));
			if (relative.StartsWith("/"))
				relative.Remove(0, 1);
			relative.ReplaceAll("/", "-");
			if (relative == desktopID)
				*result = path;
		}
	}
	closedir(dir);
}


MimeAppsParser::MimeAppsParser()
{
}


MimeAppsParser::~MimeAppsParser()
{
}


status_t
MimeAppsParser::LoadAssociations(const char* mimeAppsPath)
{
	fDefaults.clear();
	if (mimeAppsPath != NULL) {
		_LoadFile(mimeAppsPath);
		return B_OK;
	}

	BStringList paths;
	const char* configHome = getenv("XDG_CONFIG_HOME");
	if (configHome != NULL && *configHome != '\0') {
		BString path(configHome);
		append_path(path, "mimeapps.list");
		paths.Add(path);
	} else {
		const char* home = getenv("HOME");
		if (home != NULL && *home != '\0') {
			BString path(home);
			append_path(path, ".config/mimeapps.list");
			paths.Add(path);
		}
	}

	BString configDirs(getenv("XDG_CONFIG_DIRS") != NULL
		? getenv("XDG_CONFIG_DIRS") : "/etc/xdg");
	add_directory_list(paths, configDirs.String(), "mimeapps.list");

	const char* dataHome = getenv("XDG_DATA_HOME");
	if (dataHome != NULL && *dataHome != '\0') {
		BString path(dataHome);
		append_path(path, "applications/mimeapps.list");
		paths.Add(path);
	} else {
		const char* home = getenv("HOME");
		if (home != NULL && *home != '\0') {
			BString path(home);
			append_path(path, ".local/share/applications/mimeapps.list");
			paths.Add(path);
		}
	}

	BString dataDirs(getenv("XDG_DATA_DIRS") != NULL
		? getenv("XDG_DATA_DIRS") : "/usr/local/share:/usr/share");
	add_directory_list(paths, dataDirs.String(), "applications");
	for (int32 i = 0; i < paths.CountStrings(); i++) {
		BString path(paths.StringAt(i));
		if (path.EndsWith("applications"))
			append_path(path, "mimeapps.list");
		_LoadFile(path.String());
	}
	return B_OK;
}


void
MimeAppsParser::_LoadFile(const char* path)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return;

	bool defaultGroup = false;
	char line[4096];
	while (fgets(line, sizeof(line), file) != NULL) {
		char* value = line;
		while (*value == ' ' || *value == '\t')
			value++;
		if (*value == '#' || *value == '\n' || *value == '\0')
			continue;
		if (*value == '[') {
			defaultGroup = strncmp(value, "[Default Applications]", 22) == 0;
			continue;
		}
		if (!defaultGroup)
			continue;

		char* equals = strchr(value, '=');
		if (equals == NULL)
			continue;
		*equals = '\0';
		char* keyEnd = equals;
		while (keyEnd > value
			&& (keyEnd[-1] == ' ' || keyEnd[-1] == '\t')) {
			*--keyEnd = '\0';
		}
		char* apps = equals + 1;
		while (*apps == ' ' || *apps == '\t')
			apps++;
		char* newline = strpbrk(apps, "\r\n");
		if (newline != NULL)
			*newline = '\0';
		char* separator = strchr(apps, ';');
		if (separator != NULL)
			*separator = '\0';
		char* appEnd = apps + strlen(apps);
		while (appEnd > apps
			&& (appEnd[-1] == ' ' || appEnd[-1] == '\t')) {
			*--appEnd = '\0';
		}
		if (*value != '\0' && *apps != '\0'
			&& fDefaults.find(value) == fDefaults.end()) {
			fDefaults[value] = apps;
		}
	}
	fclose(file);
}


const char*
MimeAppsParser::DefaultAppForMime(const char* mimeType) const
{
	if (mimeType == NULL)
		return NULL;
	std::map<std::string, std::string>::const_iterator it
		= fDefaults.find(mimeType);
	return it != fDefaults.end() ? it->second.c_str() : NULL;
}


status_t
MimeAppsParser::FindDesktopEntry(const char* desktopID, BString* path) const
{
	if (desktopID == NULL || path == NULL || *desktopID == '\0')
		return B_BAD_VALUE;

	BStringList roots;
	const char* dataHome = getenv("XDG_DATA_HOME");
	if (dataHome != NULL && *dataHome != '\0') {
		BString applications(dataHome);
		append_path(applications, "applications");
		roots.Add(applications);
	} else {
		const char* home = getenv("HOME");
		if (home != NULL && *home != '\0') {
			BString applications(home);
			append_path(applications, ".local/share/applications");
			roots.Add(applications);
		}
	}

	BString dataDirs(getenv("XDG_DATA_DIRS") != NULL
		? getenv("XDG_DATA_DIRS") : "/usr/local/share:/usr/share");
	add_directory_list(roots, dataDirs.String(), "applications");
	path->SetTo("");
	for (int32 i = 0; i < roots.CountStrings(); i++) {
		BString root(roots.StringAt(i));
		find_desktop_file(root.String(), root.String(), desktopID, path);
		if (path->Length() > 0)
			return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}
