/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Deskbar media controls. Commands use Koan's internal Be-message protocol;
 * the org.mpris.MediaPlayer2 D-Bus service is a separate implementation.
 */

#ifndef _MPRIS_REPLICANT_H
#define _MPRIS_REPLICANT_H

#include <Button.h>

class MPRISIconButton : public BButton {
public:
	MPRISIconButton(const char* name);
	MPRISIconButton(BMessage* archive);

	static BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t		Archive(BMessage* archive,
								bool deep = true) const;
	virtual	void			AttachedToWindow();
	virtual void MessageReceived(BMessage* msg);
};

#endif
