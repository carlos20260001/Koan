/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Deskbar media controls using Koan's internal Be-message protocol. */

#include "MPRISReplicant.h"

#include <Message.h>
#include <Roster.h>
#include <SupportDefs.h>
#include <stdio.h>

MPRISIconButton::MPRISIconButton(const char* name)
	:
	BButton(BRect(0, 0, 23, 23), name, "M",
		new BMessage('mprc'), B_FOLLOW_NONE, B_WILL_DRAW | B_NAVIGABLE)
{
	SetToolTip("Toggle MediaPlayer playback");
}


MPRISIconButton::MPRISIconButton(BMessage* archive)
	:
	BButton(archive)
{
	SetToolTip("Toggle MediaPlayer playback");
}


BArchivable*
MPRISIconButton::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "MPRISIconButton"))
		return NULL;

	return new MPRISIconButton(archive);
}


status_t
MPRISIconButton::Archive(BMessage* archive, bool deep) const
{
	status_t status = BButton::Archive(archive, deep);
	if (status != B_OK)
		return status;

	status = archive->AddString("add_on",
		"application/x-vnd.Vitruvian-MPRISReplicant");
	if (status != B_OK)
		return status;

	return archive->AddString("class", "MPRISIconButton");
}


void
MPRISIconButton::AttachedToWindow()
{
	BButton::AttachedToWindow();
	SetTarget(this);
}


void
MPRISIconButton::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case 'mprc':
		{
			BMessage command('mprc');
			command.AddString("action", "PlayPause");
			status_t status = be_roster->Launch(
				"application/x-vnd.vos-MediaPlayer", &command);
			if (status != B_OK) {
				fprintf(stderr, "MPRIS replicant: could not launch MediaPlayer "
					"(%" B_PRId32 ")\n", status);
			}
			break;
		}
		default:
			BButton::MessageReceived(msg);
	}
}


extern "C" _EXPORT BView*
instantiate_deskbar_item(float, float)
{
	return new MPRISIconButton("MPRISReplicant");
}
