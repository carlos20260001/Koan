/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Intent Apps (intent-apps-spec Phase 4 / M6 milestone)
 */

#ifndef _INTENT_APPS_H
#define _INTENT_APPS_H

#include <String.h>

class IntentApps {
public:
	IntentApps();
	~IntentApps();

	status_t ResolveIntent(const char* intentScheme, BString& outApp);
};

#endif
