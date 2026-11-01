/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * FHS Reference (fhs-spec Phase 4 / M3 milestone)
 */

#ifndef _FHS_REFERENCE_H
#define _FHS_REFERENCE_H

#include <String.h>

class FHSReference {
public:
	FHSReference();
	~FHSReference();

	status_t VerifyPath(const char* path);
	BString NormalizedPath(const char* rawPath);
};

#endif
