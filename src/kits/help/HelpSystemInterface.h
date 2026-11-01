/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Help System (help-system-spec Phase 4 / M5 milestone)
 */

#ifndef _HELP_SYSTEM_INTERFACE_H
#define _HELP_SYSTEM_INTERFACE_H

#include <String.h>

class HelpSystemInterface {
public:
	HelpSystemInterface();
	~HelpSystemInterface();

	status_t IndexHelpDocument(const char* docPath);
	BString GetHelpPath(const char* docName);
};

#endif
