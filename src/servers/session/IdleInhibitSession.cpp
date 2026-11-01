/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Idle Inhibition Session Integration Implementation
 */

#include "IdleInhibitSession.h"
#include <IdleInhibitInterface.h>

IdleInhibitSession::IdleInhibitSession()
	:
	fBusName("org.freedesktop.ScreenSaver"),
	fInhibitionCookie(0),
	fInhibited(false)
{
}

IdleInhibitSession::~IdleInhibitSession()
{
}

status_t
IdleInhibitSession::Init(const char* sessionBusName)
{
	if (sessionBusName != NULL && sessionBusName[0] != '\0')
		fBusName = sessionBusName;
	return B_OK;
}

status_t
IdleInhibitSession::RegisterInhibition(const char* appName, int32 cookie)
{
	fInhibitionCookie = cookie;
	fInhibited = true;
	return B_OK;
}

status_t
IdleInhibitSession::UnregisterInhibition(int32 cookie)
{
	if (cookie == fInhibitionCookie) {
		fInhibited = false;
		fInhibitionCookie = 0;
	}
	return B_OK;
}

bool
IdleInhibitSession::IsInhibited() const
{
	return fInhibited;
}
