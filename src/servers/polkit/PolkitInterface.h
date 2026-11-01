/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Polkit (polkit-spec Phase 4 / M7 milestone)
 */

#ifndef _POLKIT_INTERFACE_H
#define _POLKIT_INTERFACE_H

#include <String.h>

class PolkitInterface {
public:
	PolkitInterface();
	~PolkitInterface();

	status_t Authorize(const char* actionId, const char* userName);
};

#endif
