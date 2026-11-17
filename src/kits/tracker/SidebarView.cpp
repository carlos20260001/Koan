/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "SidebarView.h"

#include <Application.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <MenuItem.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <SymLink.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <Window.h>

#include <fs_info.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>

#include "Commands.h"
#include "ContainerWindow.h"
#include "FavoritesMenu.h"
#include "FSUtils.h"
#include "IconCache.h"
#include "Model.h"
#include "Navigator.h"
#include "Tags.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Sidebar"


namespace BPrivate {


static const uint32 kMsgRemoveFavorite = 'SBrf';
static const uint32 kMsgOpenNewWindow = 'SBnw';
static const uint32 kMsgUnmount = 'SBum';


SidebarView::Row::~Row()
{
	delete model;
}


SidebarView::SidebarView()
	:
	BView("sidebar", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fRows(20),
	fHover(-1),
	fDropTarget(-1),
	fHeight(0)
{
	fCurrent.unset();
}


SidebarView::~SidebarView()
{
}


void
SidebarView::AttachedToWindow()
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	SetHighUIColor(B_PANEL_TEXT_COLOR);

	// mounts and unmounts, and changes to the favorites
	BVolumeRoster().StartWatching(BMessenger(this));
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) == B_OK
		&& path.Append(kGoDirectory) == B_OK) {
		create_directory(path.Path(), 0755);
		BNode node(path.Path());
		node_ref nodeRef;
		if (node.GetNodeRef(&nodeRef) == B_OK)
			watch_node(&nodeRef, B_WATCH_DIRECTORY, this);
	}

	_Rebuild();
}


void
SidebarView::DetachedFromWindow()
{
	BVolumeRoster().StopWatching();
	stop_watching(this);
}


float
SidebarView::_RowHeight() const
{
	font_height height;
	be_plain_font->GetHeight(&height);
	float text = ceilf(height.ascent + height.descent);
	float icon = be_control_look->ComposeIconSize(B_MINI_ICON).Height() + 1;
	return std::max(text, icon) + 6;
}


void
SidebarView::_AddHeader(const char* label)
{
	Row* row = new Row;
	row->type = kHeader;
	row->label = label;
	fRows.AddItem(row);
}


void
SidebarView::_AddFolder(RowType type, const entry_ref& ref, const char* label)
{
	Row* row = new Row;
	row->type = type;
	row->ref = ref;
	row->label = label;
	row->model = new Model(&ref, true);
	if (row->model->InitCheck() != B_OK) {
		delete row;
		return;
	}
	fRows.AddItem(row);
}


void
SidebarView::_Rebuild()
{
	fRows.MakeEmpty();
	fHover = -1;

	// favorites: the folders of Tracker's Go menu
	_AddHeader(B_TRANSLATE("Favorites"));
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) == B_OK
		&& path.Append(kGoDirectory) == B_OK) {
		BDirectory dir(path.Path());
		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK) {
			entry_ref linkRef;
			if (entry.GetRef(&linkRef) != B_OK)
				continue;
			BPath linkPath(&linkRef);
			BEntry target(linkPath.Path(), true);
			entry_ref ref;
			struct stat st;
			if (target.GetStat(&st) != B_OK || !S_ISDIR(st.st_mode)
				|| target.GetRef(&ref) != B_OK) {
				continue;
			}
			_AddFolder(kFavorite, ref, linkRef.name);
			if (fRows.LastItem()->type == kFavorite)
				fRows.LastItem()->device = linkRef.device();
		}
	}

	// places
	_AddHeader(B_TRANSLATE("Places"));
	struct {
		directory_which	which;
		const char*		subPath;
		const char*		label;
	} places[] = {
		{ B_USER_DIRECTORY, NULL, B_TRANSLATE("Home") },
		{ B_DESKTOP_DIRECTORY, NULL, B_TRANSLATE("Desktop") },
		{ B_USER_DIRECTORY, "Documents", B_TRANSLATE("Documents") },
		{ B_USER_DIRECTORY, "Downloads", B_TRANSLATE("Downloads") },
		{ B_SYSTEM_APPS_DIRECTORY, NULL, B_TRANSLATE("Applications") }
	};
	for (size_t i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
		BPath placePath;
		if (find_directory(places[i].which, &placePath) != B_OK)
			continue;
		if (places[i].subPath != NULL)
			placePath.Append(places[i].subPath);
		entry_ref ref;
		BEntry entry(placePath.Path());
		if (!entry.IsDirectory() || entry.GetRef(&ref) != B_OK)
			continue;
		_AddFolder(kFolder, ref, places[i].label);
	}
	BDirectory trash;
	BVolume bootVolume;
	BVolumeRoster().GetBootVolume(&bootVolume);
	if (FSGetTrashDir(&trash, bootVolume.Device()) == B_OK) {
		BEntry entry;
		entry_ref ref;
		if (trash.GetEntry(&entry) == B_OK && entry.GetRef(&ref) == B_OK)
			_AddFolder(kFolder, ref, B_TRANSLATE("Trash"));
	}

	// volumes: the ones the Desktop shows
	_AddHeader(B_TRANSLATE("Volumes"));
	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK) {
		if (!volume.IsPersistent() || volume.Capacity() == 0)
			continue;
		fs_info info;
		if (fs_stat_dev(volume.Device(), &info) != 0
			|| strcmp(info.fsh_name, "packagefs") == 0) {
			continue;
		}
		BDirectory root;
		BEntry entry;
		entry_ref ref;
		if (volume.GetRootDirectory(&root) != B_OK
			|| root.GetEntry(&entry) != B_OK || entry.GetRef(&ref) != B_OK) {
			continue;
		}
		char name[B_FILE_NAME_LENGTH];
		if (volume.GetName(name) != B_OK || name[0] == '\0')
			strlcpy(name, ref.name, sizeof(name));
		_AddFolder(kVolume, ref, name);
		if (fRows.LastItem()->type == kVolume) {
			fRows.LastItem()->device = volume.Device();
			fRows.LastItem()->ejectable = volume != bootVolume;
		}
	}

	// tags
	_AddHeader(B_TRANSLATE("Tags"));
	const BObjectList<TagInfo, true>& tags = TrackerTags();
	for (int32 i = 0; i < tags.CountItems(); i++) {
		Row* row = new Row;
		row->type = kTag;
		row->label = tags.ItemAt(i)->name;
		row->color = tags.ItemAt(i)->color;
		fRows.AddItem(row);
	}

	_Layout();
	InvalidateLayout();
	Invalidate();
}


void
SidebarView::_Layout()
{
	float rowHeight = _RowHeight();
	float y = be_control_look->DefaultItemSpacing() / 2;
	BRect bounds(Bounds());
	for (int32 i = 0; i < fRows.CountItems(); i++) {
		Row* row = fRows.ItemAt(i);
		if (row->type == kHeader && i > 0)
			y += rowHeight / 2;
		row->frame.Set(bounds.left, y, bounds.right, y + rowHeight - 1);
		y += rowHeight;
	}
	fHeight = y;
}


BSize
SidebarView::MinSize()
{
	return BSize(be_plain_font->StringWidth("M") * 8, 50);
}


BSize
SidebarView::PreferredSize()
{
	return BSize(be_plain_font->StringWidth("M") * 12, fHeight);
}


BRect
SidebarView::_EjectRect(const Row* row) const
{
	float size = row->frame.Height() - 6;
	return BRect(row->frame.right - size - 6, row->frame.top + 3,
		row->frame.right - 6, row->frame.top + 3 + size);
}


void
SidebarView::Draw(BRect updateRect)
{
	_Layout();

	float spacing = be_control_look->DefaultLabelSpacing();
	float indent = spacing * 1.5f;
	BSize iconSize = be_control_look->ComposeIconSize(B_MINI_ICON);
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	rgb_color dim = tint_color(text, text.IsDark() ? B_LIGHTEN_1_TINT
		: B_DARKEN_1_TINT);
	font_height fontHeight;
	be_plain_font->GetHeight(&fontHeight);

	for (int32 i = 0; i < fRows.CountItems(); i++) {
		Row* row = fRows.ItemAt(i);
		if (!row->frame.Intersects(updateRect))
			continue;

		float baseline = floorf(row->frame.top + (row->frame.Height()
			+ fontHeight.ascent - fontHeight.descent) / 2);

		if (row->type == kHeader) {
			BFont font(be_bold_font);
			font.SetSize(be_plain_font->Size() * 0.85f);
			SetFont(&font);
			SetHighColor(i == fDropTarget ? text : dim);
			DrawString(row->label.String(), BPoint(spacing, baseline));
			if (i == fDropTarget) {
				SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
				StrokeRect(row->frame.InsetByCopy(2, 1));
			}
			SetFont(be_plain_font);
			continue;
		}

		bool current = row->type != kTag && row->ref == fCurrent;
		if (current || i == fHover || i == fDropTarget) {
			rgb_color highlight = current
				? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
				: tint_color(base, B_DARKEN_1_TINT);
			if (i == fDropTarget)
				highlight = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
			SetHighColor(highlight);
			FillRoundRect(row->frame.InsetByCopy(3, 1), 4, 4);
		}

		float x = indent;
		BPoint iconWhere(x, row->frame.top + floorf((row->frame.Height()
			- iconSize.Height()) / 2));
		if (row->type == kTag) {
			float dot = floorf(iconSize.Height() * 0.6f);
			SetHighColor(row->color);
			FillEllipse(BRect(x + (iconSize.Width() - dot) / 2,
				iconWhere.y + (iconSize.Height() - dot) / 2,
				x + (iconSize.Width() + dot) / 2,
				iconWhere.y + (iconSize.Height() + dot) / 2));
		} else if (row->model != NULL) {
			SetDrawingMode(B_OP_ALPHA);
			IconCache::sIconCache->Draw(row->model, this, iconWhere,
				kNormalIcon, iconSize, true);
			SetDrawingMode(B_OP_COPY);
		}
		x += iconSize.Width() + spacing;

		float right = row->frame.right - spacing;
		if (row->ejectable)
			right = _EjectRect(row).left - spacing / 2;

		BString label(row->label);
		TruncateString(&label, B_TRUNCATE_END, right - x);
		SetHighColor(current ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: text);
		SetLowColor(base);
		DrawString(label.String(), BPoint(x, baseline));

		if (row->ejectable) {
			// a small eject sign: a triangle over a bar
			BRect eject(_EjectRect(row).InsetByCopy(3, 3));
			SetHighColor(i == fHover ? text : dim);
			float middle = (eject.left + eject.right) / 2;
			float barTop = eject.bottom - eject.Height() / 4;
			FillTriangle(BPoint(eject.left, barTop - 2),
				BPoint(eject.right, barTop - 2), BPoint(middle, eject.top));
			FillRect(BRect(eject.left, barTop, eject.right, eject.bottom));
		}
	}
}


int32
SidebarView::_RowAt(BPoint where) const
{
	for (int32 i = 0; i < fRows.CountItems(); i++) {
		if (fRows.ItemAt(i)->frame.Contains(where))
			return i;
	}
	return -1;
}


void
SidebarView::SetCurrent(const entry_ref* ref)
{
	if (ref == NULL) {
		fCurrent.unset();
	} else
		fCurrent = *ref;
	Invalidate();
}


void
SidebarView::_Open(const Row* row, bool newWindow)
{
	if (row->type == kTag) {
		FindTaggedItems(row->label.String());
		return;
	}

	BContainerWindow* window = dynamic_cast<BContainerWindow*>(Window());
	if (!newWindow && window != NULL && window->Navigator() != NULL) {
		BMessage message(kSwitchDirectory);
		message.AddRef("refs", &row->ref);
		message.AddInt32("action", kActionSet);
		window->PostMessage(&message);
		return;
	}

	BMessage message(B_REFS_RECEIVED);
	message.AddRef("refs", &row->ref);
	be_app->PostMessage(&message);
}


void
SidebarView::MouseDown(BPoint where)
{
	int32 index = _RowAt(where);
	Row* row = fRows.ItemAt(index);
	if (row == NULL || row->type == kHeader)
		return;

	int32 buttons = 0;
	if (Window()->CurrentMessage() != NULL)
		buttons = Window()->CurrentMessage()->GetInt32("buttons", 0);

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		_ShowMenu(row, where);
		return;
	}

	if (row->ejectable && _EjectRect(row).Contains(where)) {
		BMessage message(kUnmountVolume);
		message.AddInt32("device_id", row->device);
		be_app->PostMessage(&message);
		return;
	}

	bool newWindow = (buttons & B_TERTIARY_MOUSE_BUTTON) != 0
		|| (modifiers() & B_COMMAND_KEY) != 0;
	_Open(row, newWindow);
}


void
SidebarView::_ShowMenu(Row* row, BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("sidebar", false, false);
	if (row->type != kTag) {
		BMessage* message = new BMessage(kMsgOpenNewWindow);
		message->AddRef("refs", &row->ref);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Open in new window"),
			message));
	}
	if (row->type == kFavorite) {
		BMessage* message = new BMessage(kMsgRemoveFavorite);
		message->AddString("name", row->label);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Remove from favorites"),
			message));
	}
	if (row->ejectable) {
		BMessage* message = new BMessage(kMsgUnmount);
		message->AddInt32("device", row->device);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Unmount"), message));
	}
	if (menu->CountItems() == 0) {
		delete menu;
		return;
	}
	menu->SetTargetForItems(this);
	ConvertToScreen(&where);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, true, true);
}


void
SidebarView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	int32 index = transit == B_EXITED_VIEW ? -1 : _RowAt(where);
	int32 dropTarget = -1;
	int32 hover = -1;
	if (dragMessage != NULL && dragMessage->HasRef("refs")) {
		Row* row = fRows.ItemAt(index);
		if (row != NULL && (row->type == kFolder || row->type == kFavorite
				|| row->type == kVolume
				|| (row->type == kHeader && index == 0))) {
			dropTarget = index;
		}
	} else {
		Row* row = fRows.ItemAt(index);
		if (row != NULL && row->type != kHeader)
			hover = index;
	}

	if (hover != fHover || dropTarget != fDropTarget) {
		fHover = hover;
		fDropTarget = dropTarget;
		Invalidate();
	}
}


void
SidebarView::_Drop(BMessage* message, int32 index)
{
	Row* row = fRows.ItemAt(index);
	if (row == NULL)
		return;

	if (row->type == kHeader) {
		// folders dropped on Favorites become favorites
		BPath goPath;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &goPath, true) != B_OK
			|| goPath.Append(kGoDirectory) != B_OK) {
			return;
		}
		BDirectory goDirectory(goPath.Path());
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
			BEntry entry(&ref, true);
			BPath path;
			if (!entry.IsDirectory() || entry.GetPath(&path) != B_OK)
				continue;
			BSymLink link;
			goDirectory.CreateSymLink(ref.name, path.Path(), &link);
		}
		return;
	}

	// items dropped on a folder go there: moved on the same volume, copied
	// to another or with Option
	struct stat destStat;
	BEntry destEntry(&row->ref);
	if (destEntry.GetStat(&destStat) != B_OK)
		return;

	BObjectList<entry_ref, true>* list = new BObjectList<entry_ref, true>(10);
	bool sameVolume = true;
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		list->AddItem(new entry_ref(ref));
		if (ref.device() != destStat.st_dev)
			sameVolume = false;
	}
	uint32 mode = sameVolume && (modifiers() & B_OPTION_KEY) == 0
		? kMoveSelectionTo : kCopySelectionTo;
	if (row->ref == fCurrent) {
		delete list;
		return;
	}
	if (destEntry.GetRef(&ref) == B_OK && FSIsTrashDir(&destEntry)) {
		FSMoveToTrash(list, NULL, true);
		return;
	}
	FSMoveToFolder(list, new BEntry(destEntry), mode, NULL);
}


void
SidebarView::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && message->HasRef("refs")) {
		BPoint where = message->DropPoint();
		ConvertFromScreen(&where);
		_Drop(message, _RowAt(where));
		fDropTarget = -1;
		Invalidate();
		return;
	}

	switch (message->what) {
		case B_NODE_MONITOR:
			// a volume came or went, or the favorites changed
			_Rebuild();
			break;

		case kMsgOpenNewWindow:
		{
			BMessage open(B_REFS_RECEIVED);
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				open.AddRef("refs", &ref);
				be_app->PostMessage(&open);
			}
			break;
		}

		case kMsgRemoveFavorite:
		{
			BPath path;
			if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) == B_OK
				&& path.Append(kGoDirectory) == B_OK
				&& path.Append(message->GetString("name", "")) == B_OK) {
				BEntry(path.Path()).Remove();
			}
			break;
		}

		case kMsgUnmount:
		{
			BMessage unmount(kUnmountVolume);
			unmount.AddInt32("device_id", message->GetInt32("device", -1));
			be_app->PostMessage(&unmount);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


} // namespace BPrivate
