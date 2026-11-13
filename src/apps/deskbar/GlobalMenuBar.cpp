/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: MIT
 */


#include "GlobalMenuBar.h"

#include <strings.h>

#include <Application.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Roster.h>
#include <Window.h>

#include "tracker_private.h"

#include "BarApp.h"
#include "BarWindow.h"
#include "WindowMenu.h"


static const uint32 kRetryUpdate = 'CmRu';
static const bigtime_t kRetryDelay = 250000;


// A BarTeamInfo copy owns its icon copy, the original's icon lives in
// its icon cache.
static void
delete_team_info_copy(BarTeamInfo* info)
{
	delete info->icon;
	info->icon = NULL;
	delete info;
}


// The menu item owns its copy of the team info: TWindowMenu keeps a
// pointer to its team list for as long as the item lives.
class TGlobalAppMenuItem : public BMenuItem {
public:
	TGlobalAppMenuItem(BarTeamInfo* info)
		:
		BMenuItem(new TWindowMenu(info->teams, info->sig)),
		fInfo(info)
	{
		SetLabel(info->name);
	}

	virtual ~TGlobalAppMenuItem()
	{
		// the submenu only holds on to the list, it does not use it
		// while it is deleted by ~BMenuItem()
		delete_team_info_copy(fInfo);
	}

	const BarTeamInfo* Info() const { return fInfo; }

private:
	BarTeamInfo*	fInfo;
};


//	#pragma mark - TGlobalAppMenuBar


TGlobalAppMenuBar::TGlobalAppMenuBar(BRect frame)
	:
	BMenuBar(frame, "GlobalAppMenuBar", B_FOLLOW_NONE, B_ITEMS_IN_ROW,
		false),
	fActiveTeam(-1),
	fAppItem(NULL),
	fUpdatePending(false)
{
	SetFont(be_bold_font);
	float spacing = be_control_look->DefaultLabelSpacing();
	SetItemMargins(spacing * 2, 0, spacing * 2, 0);
}


TGlobalAppMenuBar::~TGlobalAppMenuBar()
{
	_ClearTeamList();
}


void
TGlobalAppMenuBar::AttachedToWindow()
{
	BMenuBar::AttachedToWindow();

	_Resubscribe();

	app_info info;
	if (be_roster->GetActiveAppInfo(&info) == B_OK)
		fActiveTeam = info.team;

	_Update();
}


void
TGlobalAppMenuBar::DetachedFromWindow()
{
	BMessage message(kUnsubscribe);
	message.AddMessenger("messenger", BMessenger(this));
	be_app->PostMessage(&message);

	BMenuBar::DetachedFromWindow();
}


void
TGlobalAppMenuBar::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_SOME_APP_LAUNCHED:
		{
			// the subscription hands over a team list and an icon copy
			BList* teams = NULL;
			if (message->FindPointer("teams", (void**)&teams) == B_OK)
				delete teams;

			BBitmap* icon = NULL;
			if (message->FindPointer("icon", (void**)&icon) == B_OK)
				delete icon;

			_Resubscribe();
			_Update();
			break;
		}

		case kAddTeam:
		case kRemoveTeam:
		case B_SOME_APP_QUIT:
			_Resubscribe();
			_Update();
			break;

		case kRetryUpdate:
			fUpdatePending = false;
			_Update();
			break;

		default:
			BMenuBar::MessageReceived(message);
			break;
	}
}


void
TGlobalAppMenuBar::Draw(BRect updateRect)
{
	// plain menu background like the Deskbar menu next to it
	BMenu::Draw(updateRect);

	BRect bounds(Bounds());
	SetHighColor(tint_color(ui_color(B_MENU_BACKGROUND_COLOR),
		B_DARKEN_1_TINT));
	StrokeLine(bounds.LeftBottom(), bounds.RightBottom());
}


void
TGlobalAppMenuBar::SetActiveTeam(team_id team)
{
	// Deskbar itself and background applications are not in the team
	// list; activating them keeps the previous application shown.
	if (_InfoForTeam(team) == NULL)
		return;

	if (fAppItem != NULL
		&& fAppItem->Info()->teams->HasItem((void*)(addr_t)team)) {
		fActiveTeam = team;
		return;
	}

	fActiveTeam = team;
	_Update();
}


team_id
TGlobalAppMenuBar::ShownTeam() const
{
	if (fAppItem == NULL || fAppItem->Info()->teams->CountItems() == 0)
		return -1;

	return (team_id)(addr_t)fAppItem->Info()->teams->ItemAt(0);
}


void
TGlobalAppMenuBar::_Resubscribe()
{
	_ClearTeamList();
	TBarApp::Subscribe(BMessenger(this), &fTeamList);
}


void
TGlobalAppMenuBar::_ClearTeamList()
{
	for (int32 i = fTeamList.CountItems() - 1; i >= 0; i--)
		delete_team_info_copy((BarTeamInfo*)fTeamList.ItemAt(i));

	fTeamList.MakeEmpty();
}


BarTeamInfo*
TGlobalAppMenuBar::_InfoForTeam(team_id team) const
{
	if (team < 0)
		return NULL;

	for (int32 i = 0; i < fTeamList.CountItems(); i++) {
		BarTeamInfo* info = (BarTeamInfo*)fTeamList.ItemAt(i);
		if (info->teams->HasItem((void*)(addr_t)team))
			return info;
	}

	return NULL;
}


void
TGlobalAppMenuBar::_Update()
{
	// Replacing the item while its menu is open would pull the team list
	// from under TWindowMenu; try again once the menu has closed.
	TBarWindow* window = dynamic_cast<TBarWindow*>(Window());
	if (window != NULL && window->IsShowingMenu()) {
		if (!fUpdatePending) {
			fUpdatePending = true;
			BMessage retry(kRetryUpdate);
			BMessageRunner::StartSending(BMessenger(this), &retry,
				kRetryDelay, 1);
		}
		return;
	}

	BarTeamInfo* info = _InfoForTeam(fActiveTeam);
	if (info == NULL) {
		// the active application quit; fall back to Tracker, as the
		// desktop is what is left in front
		for (int32 i = 0; i < fTeamList.CountItems(); i++) {
			BarTeamInfo* candidate = (BarTeamInfo*)fTeamList.ItemAt(i);
			if (strcasecmp(candidate->sig, kTrackerSignature) == 0) {
				info = candidate;
				break;
			}
		}
	}

	if (fAppItem != NULL) {
		RemoveItem(fAppItem);
		delete fAppItem;
		fAppItem = NULL;
	}

	if (info != NULL) {
		fAppItem = new TGlobalAppMenuItem(new BarTeamInfo(*info));
		AddItem(fAppItem);
	}

	Invalidate();
}
