/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Menu Parser (menu-spec Phase 3 / M3 milestone)
 * Parses desktop menu categories from .desktop entries.
 */

#ifndef _MENU_PARSER_H
#define _MENU_PARSER_H

#include <String.h>
#include <List.h>

class MenuParser {
public:
	MenuParser();
	~MenuParser();

	status_t ParseDesktopEntries(const char* appsPath);
	BList* GetCategories();
};

#endif
