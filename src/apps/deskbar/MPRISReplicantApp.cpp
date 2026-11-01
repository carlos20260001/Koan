/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * MPRIS Replicant App Entry
 */

#include <Application.h>
#include <Deskbar.h>
#include <stdio.h>

#include "MPRISReplicant.h"

const char* kSignature = "application/x-vnd.Vitruvian-MPRISReplicant";

class MPRISApp : public BApplication {
public:
	MPRISApp();
	virtual void ReadyToRun();
};

MPRISApp::MPRISApp()
	:
	BApplication(kSignature)
{
}

void
MPRISApp::ReadyToRun()
{
	BDeskbar deskbar;
	if (!deskbar.HasItem("MPRISReplicant")) {
		MPRISIconButton* view = new MPRISIconButton("MPRISReplicant");
		status_t status = view != NULL ? deskbar.AddItem(view) : B_NO_MEMORY;
		if (status != B_OK) {
			fprintf(stderr, "Media controls: failed to add Deskbar item (%d)\n",
				(int)status);
		}
		delete view;
	}

	PostMessage(B_QUIT_REQUESTED);
}


int
main()
{
	MPRISApp app;
	app.Run();
	return 0;
}
