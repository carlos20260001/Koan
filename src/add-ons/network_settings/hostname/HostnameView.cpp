/*
 * Copyright 2019 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *      Adrien Destugues, pulkomandy@pulkomandy.tk
 *		Rob Gill, <rrobgill@protonmail.com>
 */


#include "HostnameView.h"

#include <stdio.h>
#include <string.h>

#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <SeparatorView.h>
#include <StringView.h>

#ifdef __linux__
	// After the Be headers: GLib defines TRUE/FALSE as well.
#	include <syslog.h>
#	include <unistd.h>
#	include <gio/gio.h>
#endif


static const int32 kMsgApply = 'aply';


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "HostnameView"


HostnameView::HostnameView(BNetworkSettingsItem* item)
	:
	BView("hostname", 0),
	fItem(item)
{
	BStringView* titleView = new BStringView("title",
		B_TRANSLATE("Hostname settings"));
	titleView->SetFont(be_bold_font);
	titleView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fHostname = new BTextControl(B_TRANSLATE("Hostname:"), "", NULL);
	fApplyButton = new BButton(B_TRANSLATE("Apply"), new BMessage(kMsgApply));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.Add(titleView)
		.Add(fHostname)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fApplyButton);

	_LoadHostname();
}


HostnameView::~HostnameView()
{
}

status_t
HostnameView::Revert()
{
	return B_OK;
}

bool
HostnameView::IsRevertable() const
{
	return false;
}


void
HostnameView::AttachedToWindow()
{
	fApplyButton->SetTarget(this);
}


void
HostnameView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgApply:
			if (_SaveHostname() == B_OK)
				fItem->NotifySettingsUpdated();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


status_t
HostnameView::_LoadHostname()
{
	BString fHostnameString;
	char hostname[MAXHOSTNAMELEN];

	if (gethostname(hostname, MAXHOSTNAMELEN) == 0) {

		fHostnameString.SetTo(hostname, MAXHOSTNAMELEN);
		fHostname->SetText(fHostnameString);

		return B_OK;
	}

	return B_ERROR;
}


status_t
HostnameView::_SaveHostname()
{
	BString hostnamestring("");

	size_t hostnamelen(strlen(fHostname->Text()));

	if (hostnamelen == 0)
		return B_ERROR;

	if (hostnamelen > MAXHOSTNAMELEN) {
		hostnamestring.Truncate(MAXHOSTNAMELEN);
		hostnamelen = MAXHOSTNAMELEN;
	}

	hostnamestring << fHostname->Text();

#ifdef __linux__
	// The static host name belongs to systemd-hostnamed, which writes
	// /etc/hostname, updates the kernel's name and asks polkit whether this
	// user may change it.
	(void)hostnamelen;
	GError* error = NULL;
	GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if (bus == NULL) {
		syslog(LOG_WARNING, "Hostname: no system bus: %s", error->message);
		g_error_free(error);
		return B_ERROR;
	}
	GVariant* result = g_dbus_connection_call_sync(bus,
		"org.freedesktop.hostname1", "/org/freedesktop/hostname1",
		"org.freedesktop.hostname1", "SetStaticHostname",
		g_variant_new("(sb)", hostnamestring.String(), TRUE), NULL,
		G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION, -1, NULL, &error);
	g_object_unref(bus);
	if (result == NULL) {
		syslog(LOG_WARNING, "Hostname: not set: %s", error->message);
		g_error_free(error);
		return B_PERMISSION_DENIED;
	}
	g_variant_unref(result);
	return B_OK;
#else
	if (sethostname(hostnamestring, hostnamelen) == 0)
		return B_OK;

	return B_ERROR;
#endif
}
