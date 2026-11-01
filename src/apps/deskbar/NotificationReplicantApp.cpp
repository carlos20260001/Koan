/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Notification Replicant - Main Entry
 * Deskbar shelf add-on that toggles the notification window.
 */

#include <Application.h>
#include <Deskbar.h>
#include <stdio.h>

#include "NotificationReplicant.h"

const char* kSignature = "application/x-vnd.Vitruvian-NotificationReplicant";


class NotificationReplicantApp : public BApplication {
public:
	NotificationReplicantApp();
	virtual ~NotificationReplicantApp();

	virtual void ReadyToRun();
};


NotificationReplicantApp::NotificationReplicantApp()
	:
	BApplication(kSignature)
{
}

NotificationReplicantApp::~NotificationReplicantApp()
{
}

void
NotificationReplicantApp::ReadyToRun()
{
	BDeskbar deskbar;
	if (!deskbar.HasItem("NotificationReplicant")) {
		NotificationIconButton* view
			= new NotificationIconButton("NotificationReplicant");
		status_t status = view != NULL ? deskbar.AddItem(view) : B_NO_MEMORY;
		if (status != B_OK) {
			fprintf(stderr, "Notification replicant: failed to add Deskbar "
				"item (%d)\n", (int)status);
		}
		delete view;
	}

	PostMessage(B_QUIT_REQUESTED);
}


int
main()
{
	NotificationReplicantApp app;
	app.Run();
	return 0;
}
