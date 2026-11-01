/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Deskbar button for showing and hiding the notification window. */

#ifndef _NOTIFICATION_REPLICANT_H
#define _NOTIFICATION_REPLICANT_H

#include <Button.h>

class NotificationIconButton : public BButton {
public:
	NotificationIconButton(const char* name);
	NotificationIconButton(BMessage* archive);

	static BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t		Archive(BMessage* archive,
								bool deep = true) const;
	virtual	void			AttachedToWindow();
	virtual void MessageReceived(BMessage* msg);
};

#endif // _NOTIFICATION_REPLICANT_H
