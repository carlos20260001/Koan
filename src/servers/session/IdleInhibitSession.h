/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Idle Inhibition Session Integration
 * Connects janus_session with idle inhibition service (Phase 2 spec).
 */

#ifndef _IDLE_INHIBIT_SESSION_H
#define _IDLE_INHIBIT_SESSION_H

#include <Message.h>

// Idle inhibition integration for session manager
// References: org.freedesktop.ScreenSaver (Phase 1 + Phase 2 spec)

class IdleInhibitSession {
public:
	IdleInhibitSession();
	~IdleInhibitSession();

	status_t	Init(const char* sessionBusName);
	status_t	RegisterInhibition(const char* appName, int32 cookie);
	status_t	UnregisterInhibition(int32 cookie);
	bool		IsInhibited() const;

private:
	BString		fBusName;
	int32		fInhibitionCookie;
	bool		fInhibited;
};

#endif
