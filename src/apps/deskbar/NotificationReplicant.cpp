/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "NotificationReplicant.h"

#include <Message.h>
#include <Messenger.h>
#include <Notifications.h>
#include <SupportDefs.h>
#include <stdio.h>


static const char* kNotificationReplicantSignature
	= "application/x-vnd.Vitruvian-NotificationReplicant";


NotificationIconButton::NotificationIconButton(const char* name)
	:
	BButton(BRect(0, 0, 23, 23), name, "N", new BMessage('noti'),
		B_FOLLOW_NONE, B_WILL_DRAW | B_NAVIGABLE)
{
	SetToolTip("Show notifications");
}


NotificationIconButton::NotificationIconButton(BMessage* archive)
	:
	BButton(archive)
{
	SetToolTip("Show notifications");
}


BArchivable*
NotificationIconButton::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "NotificationIconButton"))
		return NULL;

	return new NotificationIconButton(archive);
}


status_t
NotificationIconButton::Archive(BMessage* archive, bool deep) const
{
	status_t status = BButton::Archive(archive, deep);
	if (status != B_OK)
		return status;

	status = archive->AddString("add_on", kNotificationReplicantSignature);
	if (status != B_OK)
		return status;

	return archive->AddString("class", "NotificationIconButton");
}


void
NotificationIconButton::AttachedToWindow()
{
	BButton::AttachedToWindow();
	SetTarget(this);
}


void
NotificationIconButton::MessageReceived(BMessage* message)
{
	if (message->what == 'noti') {
		BMessenger messenger(kNotificationServerSignature);
		BMessage request(kNotificationCenterToggle);
		status_t status = messenger.SendMessage(&request);
		if (status != B_OK) {
			fprintf(stderr, "Notification replicant: server unavailable (%d)\n",
				(int)status);
		}
		return;
	}

	BButton::MessageReceived(message);
}


extern "C" _EXPORT BView*
instantiate_deskbar_item(float, float)
{
	return new NotificationIconButton("NotificationReplicant");
}
