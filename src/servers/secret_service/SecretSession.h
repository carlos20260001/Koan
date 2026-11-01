/*
 * Secret Service Session Implementation (M1 milestone)
 */

/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SecretServiceInterface.h"

#include <String.h>

class SecretSession {
public:
	SecretSession();
	~SecretSession();

	status_t OpenSession(const char* algorithm, int32 flags);
	status_t CloseSession();
};
