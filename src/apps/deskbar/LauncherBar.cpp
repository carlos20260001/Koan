/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: MIT
 */


#include "LauncherBar.h"

#include <algorithm>
#include <strings.h>

#include <Application.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Roster.h>
#include <Screen.h>

#include "tracker_private.h"

#include "BarApp.h"
#include "ShowHideMenuItem.h"
#include "TeamMenu.h"
#include "WindowMenu.h"


static const uint32 kRetryRebuild = 'CdRb';
static const bigtime_t kRetryDelay = 250000;

// logical sizes; icons are rendered at the screen's scale
static const int32 kLauncherIconSize = 48;
static const float kLauncherIconPadding = 6.0f;
static const float kIndicatorSpace = 8.0f;
static const float kIndicatorRadius = 2.0f;
static const float kLauncherPadding = 6.0f;
static const float kLauncherScreenMargin = 4.0f;


static void
delete_team_info_copy(BarTeamInfo* info)
{
	// a BarTeamInfo copy owns its icon copy
	delete info->icon;
	info->icon = NULL;
	delete info;
}


//	#pragma mark - TLauncherBarItem


// One running application. The item owns its copy of the team info, as
// its TWindowMenu keeps a pointer to the team list.
class TLauncherBarItem : public BMenuItem {
public:
	TLauncherBarItem(BarTeamInfo* info, BBitmap* icon)
		:
		BMenuItem(new TWindowMenu(info->teams, info->sig)),
		fInfo(info),
		fIcon(icon)
	{
		SetLabel(info->name);
	}

	virtual ~TLauncherBarItem()
	{
		delete fIcon;
		delete_team_info_copy(fInfo);
	}

	const BarTeamInfo* Info() const { return fInfo; }

	bool HasTeam(team_id team) const
	{
		return fInfo->teams->HasItem((void*)(addr_t)team);
	}

	float IconSide() const
	{
		// LogicalBounds(): the icon's size in view coordinates
		if (fIcon != NULL)
			return fIcon->LogicalBounds().Width() + 1;
		return be_control_look->ComposeIconSize(kLauncherIconSize).Width() + 1;
	}

	float ItemWidth() const { return IconSide() + 2 * kLauncherIconPadding; }
	float ItemHeight() const
		{ return kLauncherIconPadding + IconSide() + kIndicatorSpace; }

protected:
	virtual void GetContentSize(float* width, float* height)
	{
		*width = ItemWidth();
		*height = ItemHeight();
	}

	virtual void Draw()
	{
		BMenu* menu = Menu();
		if (menu == NULL)
			return;

		BRect frame(Frame());
		rgb_color base = menu->ViewColor();

		menu->PushState();

		if (IsSelected()) {
			// the window menu is open
			BRect highlight(frame.InsetByCopy(2, 2));
			menu->SetHighColor(tint_color(base, B_DARKEN_2_TINT));
			menu->FillRoundRect(highlight, 6, 6);
		}

		float side = IconSide();
		if (fIcon != NULL) {
			BPoint where(frame.left + floorf((frame.Width() - side) / 2),
				frame.top + kLauncherIconPadding);
			menu->SetDrawingMode(B_OP_ALPHA);
			menu->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			menu->DrawBitmap(fIcon, where);
			menu->SetDrawingMode(B_OP_COPY);
		}

		// running indicator; the active application's is highlighted
		TLauncherBar* launcher = dynamic_cast<TLauncherBar*>(menu);
		bool active = launcher != NULL && HasTeam(launcher->ActiveTeam());
		BPoint center((frame.left + frame.right) / 2,
			frame.top + kLauncherIconPadding + side + kIndicatorSpace / 2);
		menu->SetHighColor(active ? ui_color(B_KEYBOARD_NAVIGATION_COLOR)
			: tint_color(base, B_DARKEN_4_TINT));
		menu->FillEllipse(center, kIndicatorRadius, kIndicatorRadius);

		menu->PopState();
	}

private:
	BarTeamInfo*	fInfo;
	BBitmap*		fIcon;
};


//	#pragma mark - TLauncherBarBackground


class TLauncherBarBackground : public BView {
public:
	TLauncherBarBackground(BRect frame)
		:
		BView(frame, "LauncherBarBackground", B_FOLLOW_ALL, B_WILL_DRAW)
	{
		SetViewUIColor(B_MENU_BACKGROUND_COLOR);
	}

	virtual void Draw(BRect updateRect)
	{
		BRect bounds(Bounds());
		rgb_color base = ViewColor();
		SetHighColor(tint_color(base, B_DARKEN_2_TINT));
		StrokeRect(bounds);
		bounds.InsetBy(1, 1);
		SetHighColor(tint_color(base, B_LIGHTEN_1_TINT));
		StrokeLine(bounds.LeftTop(), bounds.RightTop());
	}
};


//	#pragma mark - TLauncherBarWindow


TLauncherBarWindow::TLauncherBarWindow()
	:
	BWindow(BRect(0, 0, 99, 59), "Launcher", B_NO_BORDER_WINDOW_LOOK,
		B_FLOATING_ALL_WINDOW_FEEL,
		B_WILL_ACCEPT_FIRST_CLICK | B_NOT_ZOOMABLE | B_NOT_CLOSABLE
			| B_NOT_MINIMIZABLE | B_NOT_MOVABLE | B_NOT_RESIZABLE
			| B_ASYNCHRONOUS_CONTROLS,
		B_ALL_WORKSPACES),
	fMenusShown(0)
{
	fBackground = new TLauncherBarBackground(Bounds());
	AddChild(fBackground);

	fLauncher = new TLauncherBar();
	fBackground->AddChild(fLauncher);

	UpdatePlacement();
}


void
TLauncherBarWindow::MenusBeginning()
{
	fMenusShown++;
	BWindow::MenusBeginning();
}


void
TLauncherBarWindow::MenusEnded()
{
	fMenusShown--;
	BWindow::MenusEnded();
}


void
TLauncherBarWindow::ScreenChanged(BRect frame, color_space mode)
{
	BWindow::ScreenChanged(frame, mode);
	UpdatePlacement();
}


void
TLauncherBarWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kUpdateLauncherPlacement:
			UpdatePlacement();
			break;

		case B_SOME_APP_ACTIVATED:
			fLauncher->SetActiveTeam(message->GetInt32("be:team", -1));
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
TLauncherBarWindow::UpdatePlacement()
{
	float width;
	float height;
	fLauncher->GetLauncherSize(&width, &height);

	fLauncher->MoveTo(kLauncherPadding, kLauncherPadding);
	fLauncher->ResizeTo(width - 1, height - 1);

	float windowWidth = width + 2 * kLauncherPadding - 1;
	float windowHeight = height + 2 * kLauncherPadding - 1;
	ResizeTo(windowWidth, windowHeight);

	BRect screenFrame = BScreen(this).Frame();
	MoveTo(floorf((screenFrame.left + screenFrame.right - windowWidth) / 2),
		screenFrame.bottom - windowHeight - kLauncherScreenMargin);
}


//	#pragma mark - TLauncherBar


TLauncherBar::TLauncherBar()
	:
	BMenuBar(BRect(0, 0, 9, 9), "LauncherBar", B_FOLLOW_NONE,
		B_ITEMS_IN_ROW, false),
	fActiveTeam(-1),
	fRebuildPending(false),
	fLastToolTipIndex(-1)
{
	SetItemMargins(0, 0, 0, 0);
}


TLauncherBar::~TLauncherBar()
{
	_ClearTeamList();
}


void
TLauncherBar::AttachedToWindow()
{
	BMenuBar::AttachedToWindow();
	SetViewUIColor(B_MENU_BACKGROUND_COLOR);

	_ClearTeamList();
	TBarApp::Subscribe(BMessenger(this), &fTeamList);

	app_info info;
	if (be_roster->GetActiveAppInfo(&info) == B_OK)
		fActiveTeam = info.team;

	_Rebuild();
}


void
TLauncherBar::DetachedFromWindow()
{
	BMessage message(kUnsubscribe);
	message.AddMessenger("messenger", BMessenger(this));
	be_app->PostMessage(&message);

	BMenuBar::DetachedFromWindow();
}


void
TLauncherBar::MessageReceived(BMessage* message)
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
		}
		// fall through
		case kAddTeam:
		case kRemoveTeam:
		case B_SOME_APP_QUIT:
			_ClearTeamList();
			TBarApp::Subscribe(BMessenger(this), &fTeamList);
			_Rebuild();
			break;

		case kRetryRebuild:
			fRebuildPending = false;
			_Rebuild();
			break;

		default:
			BMenuBar::MessageReceived(message);
			break;
	}
}


void
TLauncherBar::Draw(BRect updateRect)
{
	// skip the BMenuBar gradient, the background view frames the launcher
	BMenu::Draw(updateRect);
}


void
TLauncherBar::DrawBackground(BRect updateRect)
{
	SetHighColor(ViewColor());
	FillRect(updateRect);
}


void
TLauncherBar::MouseDown(BPoint where)
{
	TLauncherBarItem* item
		= dynamic_cast<TLauncherBarItem*>(ItemAt(_IndexAt(where)));
	BMessage* message = Window()->CurrentMessage();
	if (item == NULL || message == NULL)
		return;

	int32 buttons = 0;
	int32 modifiers = 0;
	message->FindInt32("buttons", &buttons);
	message->FindInt32("modifiers", &modifiers);

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0
		|| (modifiers & B_CONTROL_KEY) != 0) {
		// the application's window menu; the tooltip would cover it
		HideToolTip();
		BMenuBar::MouseDown(where);
		return;
	}

	// bring the application's windows to the front, minimized ones too
	const BList* teams = item->Info()->teams;
	TShowHideMenuItem::TeamShowHideCommon(B_BRING_TO_FRONT, teams,
		ConvertToScreen(item->Frame()), true);
	if (teams->CountItems() > 0)
		be_roster->ActivateApp((team_id)(addr_t)teams->ItemAt(0));
}


void
TLauncherBar::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	int32 index = transit == B_EXITED_VIEW ? -1 : _IndexAt(where);
	if (index != fLastToolTipIndex) {
		BMenuItem* item = ItemAt(index);
		SetToolTip(item != NULL ? item->Label() : NULL);
		fLastToolTipIndex = index;
	}

	BMenuBar::MouseMoved(where, transit, dragMessage);
}


void
TLauncherBar::GetLauncherSize(float* width, float* height)
{
	float itemSide = be_control_look->ComposeIconSize(kLauncherIconSize).Width()
		+ 1;
	*width = itemSide + 2 * kLauncherIconPadding;
	*height = kLauncherIconPadding + itemSide + kIndicatorSpace;

	if (CountItems() > 0)
		*width = 0;

	for (int32 i = 0; i < CountItems(); i++) {
		TLauncherBarItem* item
			= dynamic_cast<TLauncherBarItem*>(ItemAt(i));
		if (item == NULL)
			continue;

		*width += item->ItemWidth();
		*height = std::max(*height, item->ItemHeight());
	}

	*width = ceilf(*width);
	*height = ceilf(*height);
}


void
TLauncherBar::SetActiveTeam(team_id team)
{
	if (team == fActiveTeam)
		return;

	fActiveTeam = team;
	Invalidate();
}


void
TLauncherBar::_Rebuild()
{
	// Replacing the items while a window menu is open would pull the team
	// list from under TWindowMenu; try again once the menu has closed.
	TLauncherBarWindow* window
		= dynamic_cast<TLauncherBarWindow*>(Window());
	if (window != NULL && window->IsShowingMenu()) {
		if (!fRebuildPending) {
			fRebuildPending = true;
			BMessage retry(kRetryRebuild);
			BMessageRunner::StartSending(BMessenger(this), &retry,
				kRetryDelay, 1);
		}
		return;
	}

	RemoveItems(0, CountItems(), true);

	desk_settings* settings = static_cast<TBarApp*>(be_app)->Settings();
	if (settings->sortRunningApps)
		fTeamList.SortItems(TTeamMenu::CompareByName);

	TBarApp* barApp = static_cast<TBarApp*>(be_app);
	for (int32 i = 0; i < fTeamList.CountItems(); i++) {
		BarTeamInfo* info = (BarTeamInfo*)fTeamList.ItemAt(i);
		if (info->teams->CountItems() == 0)
			continue;

		BBitmap* icon = barApp->CopyTeamIcon(
			(team_id)(addr_t)info->teams->ItemAt(0), kLauncherIconSize);
		if (icon == NULL && info->icon != NULL)
			icon = new BBitmap(info->icon);

		TLauncherBarItem* item
			= new TLauncherBarItem(new BarTeamInfo(*info), icon);

		if (settings->trackerAlwaysFirst
			&& strcasecmp(info->sig, kTrackerSignature) == 0) {
			AddItem(item, 0);
		} else
			AddItem(item);
	}

	fLastToolTipIndex = -1;
	SetToolTip((const char*)NULL);

	if (window != NULL)
		window->UpdatePlacement();

	Invalidate();
}


void
TLauncherBar::_ClearTeamList()
{
	for (int32 i = fTeamList.CountItems() - 1; i >= 0; i--)
		delete_team_info_copy((BarTeamInfo*)fTeamList.ItemAt(i));

	fTeamList.MakeEmpty();
}


int32
TLauncherBar::_IndexAt(BPoint where)
{
	for (int32 i = 0; i < CountItems(); i++) {
		if (ItemAt(i)->Frame().Contains(where))
			return i;
	}

	return -1;
}
