/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/


#include "Navigator.h"

#include <algorithm>

#include <Bitmap.h>
#include <Button.h>
#include <CardLayout.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Locale.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageFilter.h>
#include <NodeInfo.h>
#include <Volume.h>
#include <PopUpMenu.h>
#include <TextControl.h>
#include <Window.h>

#include "Bitmaps.h"
#include "BreadcrumbView.h"
#include "Attributes.h"
#include "Commands.h"
#include "FindPanel.h"
#include "MimeTypes.h"
#include "PoseView.h"
#include "Utilities.h"
#include "FSUtils.h"
#include "Tracker.h"


namespace BPrivate {

static const int32 kMaxHistory = 32;

}


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Navigator"


static BBitmap*
ViewModeIcon(uint32 mode)
{
	// simple pictures of the three views, drawn in the text color
	BRect rect(BPoint(0, 0), be_control_look->ComposeIconSize(20));
	BBitmap* bitmap = new BBitmap(rect, B_RGBA32, true);
	BView* view = new BView(rect, "icon", 0, 0);
	bitmap->AddChild(view);
	if (!bitmap->Lock()) {
		delete bitmap;
		return NULL;
	}

	view->SetHighColor(0, 0, 0, 0);
	view->SetDrawingMode(B_OP_COPY);
	view->FillRect(rect);

	rgb_color color = ui_color(B_CONTROL_TEXT_COLOR);
	view->SetHighColor(color);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);

	float size = rect.Width() + 1;
	float unit = size / 20;
	switch (mode) {
		case kIconMode:
		{
			float cell = 7 * unit;
			float gap = 2 * unit;
			float start = (size - 2 * cell - gap) / 2;
			for (int32 row = 0; row < 2; row++) {
				for (int32 column = 0; column < 2; column++) {
					BRect square(start + column * (cell + gap),
						start + row * (cell + gap), 0, 0);
					square.right = square.left + cell - 1;
					square.bottom = square.top + cell - 1;
					view->FillRoundRect(square, unit, unit);
				}
			}
			break;
		}
		case kMiniIconMode:
		{
			float cell = 4 * unit;
			for (int32 row = 0; row < 3; row++) {
				float top = 3 * unit + row * 5.5f * unit;
				view->FillRect(BRect(3 * unit, top, 3 * unit + cell - 1,
					top + cell - 1));
				view->FillRect(BRect(9 * unit, top + unit, 17 * unit,
					top + 2 * unit));
			}
			break;
		}
		default:
		{
			for (int32 row = 0; row < 4; row++) {
				float top = 4 * unit + row * 4 * unit;
				view->FillRect(BRect(3 * unit, top, 17 * unit,
					top + 1.5f * unit - 1));
			}
			break;
		}
	}

	view->Sync();
	bitmap->Unlock();
	return bitmap;
}


enum {
	kSearchThisFolder = 0,
	kSearchBelow,
	kSearchAllDisks
};


static filter_result
SearchKeyFilter(BMessage* message, BHandler** target, BMessageFilter* filter)
{
	// Escape clears the search and goes back to the items
	int32 key;
	if (message->FindInt32("raw_char", &key) == B_OK && key == B_ESCAPE) {
		BContainerWindow* window
			= dynamic_cast<BContainerWindow*>(filter->Looper());
		if (window != NULL && window->Navigator() != NULL) {
			BMessage clear(kNavigatorCommandSearchClear);
			window->PostMessage(&clear, window->Navigator());
		}
		return B_SKIP_MESSAGE;
	}

	return B_DISPATCH_MESSAGE;
}


static filter_result
LocationKeyFilter(BMessage* message, BHandler** target, BMessageFilter* filter)
{
	// Escape leaves the typed path for the folder buttons
	int32 key;
	if (message->FindInt32("raw_char", &key) == B_OK && key == B_ESCAPE) {
		BContainerWindow* window
			= dynamic_cast<BContainerWindow*>(filter->Looper());
		if (window != NULL && window->Navigator() != NULL) {
			BMessage endEditing(kNavigatorCommandEndEditing);
			window->PostMessage(&endEditing, window->Navigator());
		}
		return B_SKIP_MESSAGE;
	}

	return B_DISPATCH_MESSAGE;
}


static filter_result
HistoryButtonFilter(BMessage* message, BHandler** target, BMessageFilter* filter)
{
	// a right click on Back or Forward lists where they go
	int32 buttons = message->GetInt32("buttons", 0);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0)
		return B_DISPATCH_MESSAGE;

	BView* view = dynamic_cast<BView*>(*target);
	if (view == NULL)
		return B_DISPATCH_MESSAGE;

	BMessage history(kNavigatorCommandHistory);
	history.AddBool("back", view->Name() != NULL
		&& strcmp(view->Name(), "back") == 0);
	BPoint where(view->Bounds().LeftBottom());
	view->ConvertToScreen(&where);
	history.AddPoint("where", where);
	BContainerWindow* window = dynamic_cast<BContainerWindow*>(filter->Looper());
	if (window != NULL && window->Navigator() != NULL)
		window->PostMessage(&history, window->Navigator());
	return B_SKIP_MESSAGE;
}


//	#pragma mark - BNavigator


BNavigator::BNavigator(const Model* model)
	:
	BToolBar(),
	fBackHistory(8),
	fForwHistory(8)
{
	// Get initial path
	model->GetPath(&fPath);

	fLocation = new BTextControl("Location", "", "",
		new BMessage(kNavigatorCommandLocation));
	fLocation->SetDivider(0);
	fBreadcrumbs = new BreadcrumbView(this);
	fLocationCards = NULL;
	fEditing = false;
	fSearchScope = kSearchThisFolder;

	fSearch = new BTextControl("search", NULL, "",
		new BMessage(kNavigatorCommandSearchGo));
	fSearch->SetModificationMessage(new BMessage(kNavigatorCommandSearch));
	fSearch->SetToolTip(B_TRANSLATE("Search: typing shows only the items "
		"that match; Return searches the chosen places"));
	fSearch->SetExplicitMinSize(BSize(be_plain_font->StringWidth("M") * 9,
		B_SIZE_UNSET));
	fSearch->SetExplicitMaxSize(BSize(be_plain_font->StringWidth("M") * 14,
		B_SIZE_UNSET));

	BPopUpMenu* scopeMenu = new BPopUpMenu("scope");
	const char* scopes[] = {
		B_TRANSLATE("This folder"),
		B_TRANSLATE("This folder and below"),
		B_TRANSLATE("All disks")
	};
	for (int32 i = 0; i < 3; i++) {
		BMessage* message = new BMessage(kNavigatorCommandSearchScope);
		message->AddInt32("scope", i);
		BMenuItem* item = new BMenuItem(scopes[i], message);
		item->SetMarked(i == kSearchThisFolder);
		scopeMenu->AddItem(item);
	}
	fScopeField = new BMenuField("scope", NULL, scopeMenu);
	float scopeWidth = 0;
	for (int32 i = 0; i < 3; i++)
		scopeWidth = std::max(scopeWidth, be_plain_font->StringWidth(scopes[i]));
	fScopeField->SetExplicitMaxSize(BSize(scopeWidth
		+ be_control_look->DefaultLabelSpacing() * 6, B_SIZE_UNSET));

	GroupLayout()->SetInsets(0.0f, 0.0f, B_USE_HALF_ITEM_INSETS, 1.0f);
		// 1px bottom inset used for border

	// Needed to draw the bottom border
	SetFlags(Flags() | B_WILL_DRAW);
}


BNavigator::~BNavigator()
{
}


void
BNavigator::AttachedToWindow()
{
	BToolBar::AttachedToWindow();

	const BRect iconRect(BPoint(0, 0),
		be_control_look->ComposeIconSize(20));

	// Set up toolbar items
	BBitmap* bmpBack = new BBitmap(iconRect, B_RGBA32);
	GetTrackerResources()->GetIconResource(R_ResBackNav, B_MINI_ICON, bmpBack);
	AddAction(kNavigatorCommandBackward, this, bmpBack);
	SetActionEnabled(kNavigatorCommandBackward, false);
	delete bmpBack;

	BBitmap* bmpForw = new BBitmap(iconRect, B_RGBA32);
	GetTrackerResources()->GetIconResource(R_ResForwNav, B_MINI_ICON, bmpForw);
	AddAction(kNavigatorCommandForward, this, bmpForw);
	SetActionEnabled(kNavigatorCommandForward, false);
	delete bmpForw;

	BBitmap* bmpUp = new BBitmap(iconRect, B_RGBA32);
	GetTrackerResources()->GetIconResource(R_ResUpNav, B_MINI_ICON, bmpUp);
	AddAction(kNavigatorCommandUp, this, bmpUp);
	SetActionEnabled(kNavigatorCommandUp, false);
	delete bmpUp;

	// the path as folder buttons, or typed in after a click on the empty
	// part or Cmd-L
	BView* location = new BView("location", 0);
	fLocationCards = new BCardLayout();
	location->SetLayout(fLocationCards);
	fLocationCards->AddView(fBreadcrumbs);
	fLocationCards->AddView(fLocation);
	fLocationCards->SetVisibleItem((int32)0);
	location->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	AddView(location);
	fLocation->SetTarget(this);

	BMessageFilter* keyFilter = new BMessageFilter(B_KEY_DOWN,
		LocationKeyFilter);
	fLocation->TextView()->AddFilter(keyFilter);

	// right click on Back and Forward for their history
	uint32 historyCommands[] = { kNavigatorCommandBackward,
		kNavigatorCommandForward };
	for (int32 i = 0; i < 2; i++) {
		BButton* button = FindButton(historyCommands[i]);
		if (button == NULL)
			continue;
		button->SetName(i == 0 ? "back" : "forward");
		BMessageFilter* filter = new BMessageFilter(B_MOUSE_DOWN,
			HistoryButtonFilter);
		button->AddFilter(filter);
	}
	SetActionEnabled(kNavigatorCommandBackward, false);
	SetActionEnabled(kNavigatorCommandForward, false);

	// search
	AddView(fSearch);
	AddView(fScopeField);
	fSearch->SetTarget(this);
	fScopeField->Menu()->SetTargetForItems(this);
	BMessageFilter* searchFilter = new BMessageFilter(B_KEY_DOWN,
		SearchKeyFilter);
	fSearch->TextView()->AddFilter(searchFilter);

	// the view modes
	AddSeparator();
	BHandler* poseView = Window() != NULL ? (BHandler*)Window()->PoseView()
		: NULL;
	struct {
		uint32		mode;
		const char*	tip;
	} modes[] = {
		{ kIconMode, B_TRANSLATE("Icon view") },
		{ kMiniIconMode, B_TRANSLATE("Mini icon view") },
		{ kListMode, B_TRANSLATE("List view") }
	};
	for (int32 i = 0; i < 3; i++) {
		BBitmap* icon = ViewModeIcon(modes[i].mode);
		AddAction(modes[i].mode, poseView, icon, modes[i].tip, NULL, true);
		delete icon;
	}
	if (Window() != NULL && Window()->PoseView() != NULL)
		UpdateViewMode(Window()->PoseView()->ViewMode());
}


void
BNavigator::UpdateViewMode(uint32 mode)
{
	SetActionPressed(kIconMode, mode == kIconMode);
	SetActionPressed(kMiniIconMode, mode == kMiniIconMode);
	SetActionPressed(kListMode, mode == kListMode);
}


void
BNavigator::TakeHistory(BObjectList<BPath, true>& back,
	BObjectList<BPath, true>& forward)
{
	back.MakeEmpty();
	forward.MakeEmpty();
	while (!fBackHistory.IsEmpty())
		back.AddItem(fBackHistory.RemoveItemAt(0));
	while (!fForwHistory.IsEmpty())
		forward.AddItem(fForwHistory.RemoveItemAt(0));
}


void
BNavigator::SetHistory(BObjectList<BPath, true>& back,
	BObjectList<BPath, true>& forward)
{
	fBackHistory.MakeEmpty();
	fForwHistory.MakeEmpty();
	while (!back.IsEmpty())
		fBackHistory.AddItem(back.RemoveItemAt(0));
	while (!forward.IsEmpty())
		fForwHistory.AddItem(forward.RemoveItemAt(0));
	SetActionEnabled(kNavigatorCommandForward, fForwHistory.CountItems() > 0);
	SetActionEnabled(kNavigatorCommandBackward, fBackHistory.CountItems() > 1);
}


void
BNavigator::ClearSearch()
{
	if (fSearch->Text()[0] == '\0')
		return;

	fSearch->SetText("");
	if (Window() != NULL && Window()->PoseView() != NULL)
		Window()->PoseView()->SetSearchFilter("");
}


static BString
NamePattern(const char* text)
{
	// name contains text, any case: "*[rR][eE][pP][oO][rR][tT]*"
	BString pattern("*");
	for (const char* c = text; *c != '\0'; c++) {
		unsigned char ch = (unsigned char)*c;
		if (ch < 0x80 && isalpha(ch)) {
			pattern << '[' << (char)tolower(ch) << (char)toupper(ch) << ']';
		} else {
			if (strchr("*?[]\\\"'", ch) != NULL)
				pattern << '\\';
			pattern << (char)ch;
		}
	}
	pattern << '*';
	return pattern;
}


void
BNavigator::RunSearchQuery()
{
	// a live query, the same kind the Find window makes, opened in its own
	// window
	BString text(fSearch->Text());
	text.Trim();
	if (text.Length() == 0)
		return;

	BPath queries;
	if (find_directory(B_USER_DIRECTORY, &queries, true) != B_OK
		|| queries.Append("queries") != B_OK) {
		return;
	}
	create_directory(queries.Path(), 0755);

	BString name(B_TRANSLATE_COMMENT("Search for %text",
		"Name of the query file a search makes"));
	name.ReplaceFirst("%text", text);
	name.ReplaceAll("/", "-");
	BPath path(queries);
	path.Append(name.String());

	BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;

	BNodeInfo(&file).SetType(B_QUERY_MIMETYPE);

	BString predicate("(name==\"");
	predicate << NamePattern(text.String()) << "\")";
	file.WriteAttrString(kAttrQueryString, &predicate);

	if (fSearchScope == kSearchBelow) {
		// this volume, and only below this folder
		entry_ref folder;
		if (get_ref_for_path(fPath.Path(), &folder) == B_OK) {
			BMessage directories;
			directories.AddRef("refs", &folder);
			BString buffer;
			ssize_t size = directories.FlattenedSize();
			char* data = buffer.LockBuffer(size);
			directories.Flatten(data, size);
			file.WriteAttr("_trk/directories", B_MESSAGE_TYPE, 0, data, size);
			buffer.UnlockBuffer(0);

			BVolume volume(folder.device());
			BMessage volumes;
			EmbedUniqueVolumeInfo(&volumes, &volume);
			size = volumes.FlattenedSize();
			data = buffer.LockBuffer(size);
			volumes.Flatten(data, size);
			file.WriteAttr(kAttrQueryVolume, B_MESSAGE_TYPE, 0, data, size);
			buffer.UnlockBuffer(0);
		}
	}

	MoreOptionsStruct options;
	options.temporary = true;
	file.WriteAttr(kAttrQueryMoreOptions, B_RAW_TYPE, 0, &options,
		sizeof(options));

	int32 now = (int32)time(NULL);
	file.WriteAttr(kAttrQueryLastChange, B_INT32_TYPE, 0, &now, sizeof(now));
	file.Unset();

	entry_ref ref;
	if (get_ref_for_path(path.Path(), &ref) != B_OK)
		return;

	BMessage open(B_REFS_RECEIVED);
	open.AddRef("refs", &ref);
	be_app->PostMessage(&open);
}


void
BNavigator::StartEditing()
{
	if (fLocationCards == NULL)
		return;

	fEditing = true;
	fLocation->SetText(fPath.Path());
	fLocationCards->SetVisibleItem(1);
	fLocation->MakeFocus();
	fLocation->TextView()->SelectAll();
}


void
BNavigator::EndEditing()
{
	if (!fEditing || fLocationCards == NULL)
		return;

	fEditing = false;
	fLocation->SetText(fPath.Path());
	fLocationCards->SetVisibleItem((int32)0);
	if (Window() != NULL && Window()->PoseView() != NULL)
		Window()->PoseView()->MakeFocus();
}


void
BNavigator::EndEditingIfUnfocused()
{
	if (fEditing && Window() != NULL
		&& Window()->CurrentFocus() != fLocation->TextView()) {
		EndEditing();
	}
}


void
BNavigator::ShowHistoryMenu(bool back, BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("history", false, false);
	if (back) {
		// newest first, the current folder is the last one in the list
		for (int32 i = fBackHistory.CountItems() - 2, steps = 1; i >= 0;
				i--, steps++) {
			BMessage* message = new BMessage(kNavigatorCommandHistory);
			message->AddBool("back", true);
			message->AddInt32("steps", steps);
			menu->AddItem(new BMenuItem(fBackHistory.ItemAt(i)->Leaf(), message));
		}
	} else {
		for (int32 i = fForwHistory.CountItems() - 1, steps = 1; i >= 0;
				i--, steps++) {
			BMessage* message = new BMessage(kNavigatorCommandHistory);
			message->AddBool("back", false);
			message->AddInt32("steps", steps);
			menu->AddItem(new BMenuItem(fForwHistory.ItemAt(i)->Leaf(), message));
		}
	}

	if (menu->CountItems() == 0) {
		delete menu;
		return;
	}

	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, true, true);
}


void
BNavigator::GoHistory(bool back, int32 steps, bool option)
{
	// skip over the steps in between, the last one is a normal step
	for (int32 i = 1; i < steps; i++) {
		if (back && fBackHistory.CountItems() > 2) {
			fForwHistory.AddItem(fBackHistory.RemoveItemAt(
				fBackHistory.CountItems() - 1));
		} else if (!back && fForwHistory.CountItems() > 1) {
			fBackHistory.AddItem(fForwHistory.RemoveItemAt(
				fForwHistory.CountItems() - 1));
		}
	}

	if (back)
		GoBackward(option);
	else
		GoForward(option);
}


void
BNavigator::AllAttached()
{
	// Inital setup of widget states
	UpdateLocation(0, kActionSet);
}


void
BNavigator::Draw(BRect updateRect)
{
	// Draw a 1px bottom border, like BMenuBar
	BRect rect(Bounds());
	rgb_color base = LowColor();
	uint32 flags = 0;

	be_control_look->DrawBorder(this, rect, updateRect, base,
		B_PLAIN_BORDER, flags, BControlLook::B_BOTTOM_BORDER);

	_inherited::Draw(rect & updateRect);
}


void
BNavigator::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kNavigatorCommandBackward:
			GoBackward((modifiers() & B_OPTION_KEY) == B_OPTION_KEY);
			break;

		case kNavigatorCommandForward:
			GoForward((modifiers() & B_OPTION_KEY) == B_OPTION_KEY);
			break;

		case kNavigatorCommandUp:
			GoUp((modifiers() & B_OPTION_KEY) == B_OPTION_KEY);
			break;

		case kNavigatorCommandLocation:
			GoTo();
			break;

		case kNavigatorCommandSetFocus:
		case kBreadcrumbEdit:
			StartEditing();
			break;

		case kNavigatorCommandEndEditing:
			EndEditing();
			break;

		case kNavigatorCommandSearch:
			// the items of this folder that match, as one types
			if (Window() != NULL && Window()->PoseView() != NULL)
				Window()->PoseView()->SetSearchFilter(fSearch->Text());
			break;

		case kNavigatorCommandSearchGo:
			if (fSearchScope != kSearchThisFolder)
				RunSearchQuery();
			break;

		case kNavigatorCommandSearchFocus:
			fSearch->MakeFocus();
			fSearch->TextView()->SelectAll();
			break;

		case kNavigatorCommandSearchScope:
			fSearchScope = message->GetInt32("scope", kSearchThisFolder);
			if (fSearchScope != kSearchThisFolder && fSearch->Text()[0] != '\0')
				RunSearchQuery();
			break;

		case kNavigatorCommandSearchClear:
			ClearSearch();
			if (Window() != NULL && Window()->PoseView() != NULL)
				Window()->PoseView()->MakeFocus();
			break;

		case kNavigatorCommandHistory:
			if (message->HasInt32("steps")) {
				GoHistory(message->GetBool("back", true),
					message->GetInt32("steps", 1),
					(modifiers() & B_OPTION_KEY) != 0);
			} else {
				ShowHistoryMenu(message->GetBool("back", true),
					message->GetPoint("where", B_ORIGIN));
			}
			break;

		default:
		{
			// Catch any dropped refs and try to switch to this new directory
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BMessage message(kSwitchDirectory);
				BEntry entry(&ref, true);
				if (!entry.IsDirectory()) {
					entry.GetRef(&ref);
					BPath path(&ref);
					path.GetParent(&path);
					get_ref_for_path(path.Path(), &ref);
				}
				message.AddRef("refs", &ref);
				message.AddInt32("action", kActionSet);
				Window()->PostMessage(&message);
			}
		}
	}
}


void
BNavigator::GoBackward(bool option)
{
	int32 itemCount = fBackHistory.CountItems();
	if (itemCount >= 2 && fBackHistory.ItemAt(itemCount - 2)) {
		BEntry entry;
		if (entry.SetTo(fBackHistory.ItemAt(itemCount - 2)->Path()) == B_OK)
			SendNavigationMessage(kActionBackward, &entry, option);
	}
}


void
BNavigator::GoForward(bool option)
{
	if (fForwHistory.CountItems() >= 1) {
		BEntry entry;
		if (entry.SetTo(fForwHistory.LastItem()->Path()) == B_OK)
			SendNavigationMessage(kActionForward, &entry, option);
	}
}


void
BNavigator::GoUp(bool option)
{
	BEntry entry;
	if (entry.SetTo(fPath.Path()) == B_OK) {
		BEntry parentEntry;
		if (entry.GetParent(&parentEntry) == B_OK) {
			SendNavigationMessage(kActionUp, &parentEntry, option);
		}
	}
}


void
BNavigator::SendNavigationMessage(NavigationAction action, BEntry* entry,
	bool option)
{
	entry_ref ref;

	if (entry->GetRef(&ref) == B_OK) {
		BMessage message;
		message.AddRef("refs", &ref);
		message.AddInt32("action", action);

		// get the node of this folder for selecting it in the new location
		const node_ref* nodeRef;
		if (Window() && Window()->TargetModel())
			nodeRef = Window()->TargetModel()->NodeRef();
		else
			nodeRef = NULL;

		// if the option key was held down, open in new window (send message
		// to be_app) otherwise send message to this window. TTracker
		// (be_app) understands nodeRefToSlection, BContainerWindow doesn't,
		// so we have to select the item manually
		if (option) {
			message.what = B_REFS_RECEIVED;
			if (nodeRef != NULL) {
				message.AddData("nodeRefToSelect", B_RAW_TYPE, nodeRef,
					sizeof(node_ref));
			}
			be_app->PostMessage(&message);
		} else {
			message.what = kSwitchDirectory;
			Window()->PostMessage(&message);
			UnlockLooper();
				// This is to prevent a dead-lock situation.
				// SelectChildInParentSoon() eventually locks the
				// TaskLoop::fLock. Later, when StandAloneTaskLoop::Run()
				// runs, it also locks TaskLoop::fLock and subsequently
				// locks this window's looper. Therefore we can't call
				// SelectChildInParentSoon with our Looper locked,
				// because we would get different orders of locking
				// (thus the risk of dead-locking).
				//
				// Todo: Change the locking behaviour of
				// StandAloneTaskLoop::Run() and subsequently called
				// functions.
			if (nodeRef != NULL) {
				TTracker* tracker = dynamic_cast<TTracker*>(be_app);
				if (tracker != NULL)
					tracker->SelectChildInParentSoon(&ref, nodeRef);
			}

			LockLooper();
		}
	}
}


void
BNavigator::GoTo()
{
	BString pathname = fLocation->Text();

	if (pathname.Compare("") == 0)
		pathname = "/";

	BEntry entry;
	entry_ref ref;

	if (entry.SetTo(pathname.String()) == B_OK
		&& entry.GetRef(&ref) == B_OK) {
		EndEditing();
		BMessage message(kSwitchDirectory);
		message.AddRef("refs", &ref);
		message.AddInt32("action", kActionLocation);
		Window()->PostMessage(&message);
	} else {
		BPath path;

		if (Window() && Window()->TargetModel()) {
			Window()->TargetModel()->GetPath(&path);
			fLocation->SetText(path.Path());
		}
	}
}


void
BNavigator::UpdateLocation(const Model* newmodel, int32 action)
{
	if (newmodel)
		newmodel->GetPath(&fPath);

	// Modify history according to commands
	switch (action) {
		case kActionBackward:
			fForwHistory.AddItem(fBackHistory.RemoveItemAt(
				fBackHistory.CountItems() - 1));
			break;

		case kActionForward:
			fBackHistory.AddItem(fForwHistory.RemoveItemAt(
				fForwHistory.CountItems() - 1));
			break;

		case kActionUpdatePath:
			break;

		default:
			fForwHistory.MakeEmpty();
			fBackHistory.AddItem(new BPath(fPath));

			while (fBackHistory.CountItems() > kMaxHistory)
				fBackHistory.RemoveItem(fBackHistory.FirstItem(), true);
			break;
	}

	// Enable Up button when there is any parent
	BEntry entry;
	if (entry.SetTo(fPath.Path()) == B_OK) {
		BEntry parentEntry;
		bool enable = entry.GetParent(&parentEntry) == B_OK;
		SetActionEnabled(kNavigatorCommandUp, enable);
	}

	// Enable history buttons if history contains something
	SetActionEnabled(kNavigatorCommandForward, fForwHistory.CountItems() > 0);
	SetActionEnabled(kNavigatorCommandBackward, fBackHistory.CountItems() > 1);

	// Avoid loss of selection and cursor position
	if (action != kActionLocation)
		fLocation->SetText(fPath.Path());

	fBreadcrumbs->SetPath(fPath);

	// another folder starts without the search of the last one
	if (action != kActionUpdatePath && fSearch != NULL)
		fSearch->SetText("");
}
