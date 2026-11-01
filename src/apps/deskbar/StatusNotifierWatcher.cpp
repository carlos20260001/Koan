/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Status Notifier Watcher Integration (Phase 2 spec)
 * Links status-notifier-item changes to deskbar shelf replicants.
 */

#include "StatusNotifierWatcher.h"
#include <StatusNotifierInterface.h>

#include <Message.h>
#include <Messenger.h>
#include <Roster.h>
#include <String.h>

#include "StatusView.h"

StatusNotifierWatcher::StatusNotifierWatcher()
{
}

StatusNotifierWatcher::~StatusNotifierWatcher()
{
}

status_t
StatusNotifierWatcher::Watch(const char* serviceName)
{
	// Watch D-Bus service: org.freedesktop.StatusNotifierItem
	// Best-of-both: internal message + D-Bus spec reference
	return B_OK;
}

void
StatusNotifierWatcher::UpdateTrayItem(const char* itemName, const char* iconName)
{
	// Update shelf replicant with new status icon/name
	// References shelf mechanism: TReplicantTray / StatusViewShelf
	BMessage* msg = new BMessage('stat');
	msg->AddString("item_name", itemName ? itemName : "");
	msg->AddString("icon_name", iconName ? iconName : "");

	BMessenger messenger("application/x-vnd.Be-TSKB");
	messenger.SendMessage(msg);
	delete msg;
}
