/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * MIME Apps Parser (mime-apps spec)
 */

#ifndef _MIME_APPS_PARSER_H
#define _MIME_APPS_PARSER_H

#include <String.h>
#include <map>
#include <string>

class MimeAppsParser {
public:
	MimeAppsParser();
	~MimeAppsParser();

	status_t LoadAssociations(const char* mimeAppsPath = NULL);
	const char* DefaultAppForMime(const char* mimeType) const;
	status_t FindDesktopEntry(const char* desktopID, BString* path) const;

private:
	void _LoadFile(const char* path);

	std::map<std::string, std::string> fDefaults;
};

#endif
