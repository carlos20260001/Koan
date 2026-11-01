/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Session Manager Integration for Idle Inhibition (Phase 2 spec)
 * Prevents screen idle/locking when inhibition registered.
 */

#include "IdleInhibitSession.h"
#include <IdleInhibitInterface.h>

#include <Desktop.h>
#include <Screen.h>

// Integration point: session manager activates idle inhibition
static IdleInhibitSession* gIdleInhibit = NULL;

void
InitIdleInhibition(const char* sessionBusName)
{
	if (gIdleInhibit != NULL)
		delete gIdleInhibit;

	gIdleInhibit = new IdleInhibitSession();
	if (gIdleInhibit != NULL) {
		gIdleInhibit->Init(sessionBusName ? sessionBusName : IDLE_INHIBIT_SERVICE);
	}
}

bool
IsSessionIdleInhibited()
{
	return (gIdleInhibit != NULL && gIdleInhibit->IsInhibited());
}
