/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "BreadcrumbView.h"

#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <Locale.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Volume.h>
#include <Window.h>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <vector>

#include "Commands.h"
#include "FSUtils.h"
#include "Navigator.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BreadcrumbView"


namespace BPrivate {


static const uint32 kMsgGoPath = 'BCgo';
static const char* kArrow = "›";


BreadcrumbView::BreadcrumbView(BHandler* target)
	:
	BView("breadcrumbs", B_WILL_DRAW | B_FRAME_EVENTS),
	fTarget(target),
	fSegments(8),
	fFirstVisible(0),
	fHover(-1),
	fHoverArrow(false),
	fDropping(false)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	SetHighUIColor(B_PANEL_TEXT_COLOR);
}


void
BreadcrumbView::SetPath(const BPath& path)
{
	fSegments.MakeEmpty();

	// one segment per folder, a volume's mount point shown by its name
	BString full(path.Path());
	if (full.Length() == 0)
		full = "/";

	std::vector<BString> prefixes;
	prefixes.push_back("/");
	int32 start = 1;
	while (start < full.Length()) {
		int32 slash = full.FindFirst('/', start);
		if (slash < 0)
			slash = full.Length();
		BString prefix;
		full.CopyInto(prefix, 0, slash);
		prefixes.push_back(prefix);
		start = slash + 1;
	}

	dev_t rootDevice = -1;
	struct stat st;
	if (stat("/", &st) == 0)
		rootDevice = st.st_dev;

	bool skipRoot = false;
	for (size_t i = 0; i < prefixes.size(); i++) {
		Segment* segment = new Segment;
		segment->path = prefixes[i];
		const char* leaf = strrchr(prefixes[i].String(), '/');
		segment->label = (i == 0) ? "/" : leaf + 1;

		if (i > 0 && stat(prefixes[i].String(), &st) == 0) {
			struct stat parentStat;
			BString parent(prefixes[i - 1]);
			if (stat(parent.String(), &parentStat) == 0
				&& st.st_dev != parentStat.st_dev && st.st_dev != rootDevice) {
				// a mounted volume: its name, as on the Desktop
				BVolume volume(st.st_dev);
				char name[B_FILE_NAME_LENGTH];
				if (volume.GetName(name) == B_OK && name[0] != '\0')
					segment->label = name;
				if (i == 1)
					skipRoot = true;
			}
		}
		fSegments.AddItem(segment);
	}

	if (skipRoot && fSegments.CountItems() > 1)
		delete fSegments.RemoveItemAt(0);

	fHover = -1;
	_Layout();
	Invalidate();
}


void
BreadcrumbView::_Layout()
{
	float spacing = be_control_look->DefaultLabelSpacing();
	float arrowWidth = StringWidth(kArrow) + spacing;
	BRect bounds(Bounds());

	// leave out the first folders when they do not all fit
	float total = 0;
	for (int32 i = 0; i < fSegments.CountItems(); i++)
		total += StringWidth(fSegments.ItemAt(i)->label) + 2 * spacing + arrowWidth;

	float ellipsisWidth = StringWidth(B_UTF8_ELLIPSIS) + 2 * spacing
		+ arrowWidth;
	fFirstVisible = 0;
	while (total > bounds.Width() && fFirstVisible < fSegments.CountItems() - 1) {
		Segment* segment = fSegments.ItemAt(fFirstVisible);
		total -= StringWidth(segment->label) + 2 * spacing + arrowWidth;
		if (fFirstVisible == 0)
			total += ellipsisWidth;
		fFirstVisible++;
	}

	float x = bounds.left;
	fEllipsisFrame = BRect();
	if (fFirstVisible > 0) {
		fEllipsisFrame.Set(x, bounds.top, x + ellipsisWidth, bounds.bottom);
		x += ellipsisWidth;
	}

	for (int32 i = 0; i < fSegments.CountItems(); i++) {
		Segment* segment = fSegments.ItemAt(i);
		if (i < fFirstVisible) {
			segment->frame = BRect();
			segment->arrowFrame = BRect();
			continue;
		}
		float width = StringWidth(segment->label) + 2 * spacing;
		segment->frame.Set(x, bounds.top, x + width, bounds.bottom);
		x += width;
		segment->arrowFrame.Set(x, bounds.top, x + arrowWidth, bounds.bottom);
		x += arrowWidth;
	}
}


void
BreadcrumbView::FrameResized(float width, float height)
{
	_Layout();
	Invalidate();
}


BSize
BreadcrumbView::MinSize()
{
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	return BSize(80, ceilf(fontHeight.ascent + fontHeight.descent) + 8);
}


BSize
BreadcrumbView::PreferredSize()
{
	BSize size = MinSize();
	size.width = 400;
	return size;
}


void
BreadcrumbView::Draw(BRect updateRect)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	BRect bounds(Bounds());
	float baseline = floorf(bounds.top + (bounds.Height() + fontHeight.ascent
		- fontHeight.descent) / 2);
	float spacing = be_control_look->DefaultLabelSpacing();
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	rgb_color dim = tint_color(text, text.IsDark() ? B_LIGHTEN_1_TINT
		: B_DARKEN_1_TINT);

	if (fEllipsisFrame.IsValid()) {
		SetHighColor(dim);
		DrawString(B_UTF8_ELLIPSIS, BPoint(fEllipsisFrame.left + spacing,
			baseline));
	}

	int32 count = fSegments.CountItems();
	for (int32 i = fFirstVisible; i < count; i++) {
		Segment* segment = fSegments.ItemAt(i);
		bool last = i == count - 1;

		if (i == fHover && (!last || fDropping) && !fHoverArrow) {
			// a button look for the one under the mouse
			BRect frame(segment->frame);
			frame.InsetBy(1, 2);
			be_control_look->DrawButtonFrame(this, frame, updateRect, base,
				base, fDropping ? BControlLook::B_ACTIVATED : 0);
			be_control_look->DrawButtonBackground(this, frame, updateRect,
				base, fDropping ? BControlLook::B_ACTIVATED : 0);
		}

		SetHighColor(text);
		if (last) {
			BFont font(be_bold_font);
			font.SetSize(be_plain_font->Size());
			SetFont(&font);
		}
		SetLowColor(base);
		DrawString(segment->label.String(), BPoint(segment->frame.left + spacing,
			baseline));
		SetFont(be_plain_font);

		SetHighColor(i == fHover && fHoverArrow ? text : dim);
		DrawString(kArrow, BPoint(segment->arrowFrame.left + spacing / 2,
			baseline));
	}
}


int32
BreadcrumbView::_SegmentAt(BPoint where, bool* onArrow) const
{
	*onArrow = false;
	for (int32 i = fFirstVisible; i < fSegments.CountItems(); i++) {
		Segment* segment = fSegments.ItemAt(i);
		if (segment->frame.Contains(where))
			return i;
		if (segment->arrowFrame.Contains(where)) {
			*onArrow = true;
			return i;
		}
	}
	return -1;
}


void
BreadcrumbView::MouseDown(BPoint where)
{
	if (fEllipsisFrame.IsValid() && fEllipsisFrame.Contains(where)) {
		_ShowHiddenMenu();
		return;
	}

	bool onArrow;
	int32 index = _SegmentAt(where, &onArrow);
	if (index < 0) {
		// the empty part: type a path instead
		BMessage message(kBreadcrumbEdit);
		BMessenger(fTarget).SendMessage(&message);
		return;
	}

	if (onArrow) {
		_ShowFolderMenu(index);
		return;
	}

	if (index < fSegments.CountItems() - 1)
		_Go(fSegments.ItemAt(index)->path.String());
}


void
BreadcrumbView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	bool onArrow = false;
	int32 hover = transit == B_EXITED_VIEW ? -1 : _SegmentAt(where, &onArrow);
	bool dropping = dragMessage != NULL && dragMessage->HasRef("refs");
	if (dropping)
		onArrow = false;

	if (hover != fHover || onArrow != fHoverArrow || dropping != fDropping) {
		fHover = hover;
		fHoverArrow = onArrow;
		fDropping = dropping && hover >= 0;
		Invalidate();
	}
}


void
BreadcrumbView::_Go(const char* path)
{
	entry_ref ref;
	if (get_ref_for_path(path, &ref) != B_OK)
		return;

	BMessage message(kSwitchDirectory);
	message.AddRef("refs", &ref);
	message.AddInt32("action", kActionSet);
	Window()->PostMessage(&message);
}


void
BreadcrumbView::_ShowFolderMenu(int32 index)
{
	// the folders in this one, to go to
	Segment* segment = fSegments.ItemAt(index);
	BDirectory dir(segment->path.String());
	if (dir.InitCheck() != B_OK)
		return;

	std::vector<BString> names;
	BEntry entry;
	while (dir.GetNextEntry(&entry, true) == B_OK) {
		if (!entry.IsDirectory())
			continue;
		char name[B_FILE_NAME_LENGTH];
		entry.GetName(name);
		if (name[0] == '.')
			continue;
		names.push_back(name);
		if (names.size() > 500)
			break;
	}
	std::sort(names.begin(), names.end(),
		[](const BString& a, const BString& b) { return a.ICompare(b) < 0; });

	BPopUpMenu* menu = new BPopUpMenu("folders", false, false);
	BString current;
	if (index + 1 < fSegments.CountItems()) {
		const char* next = strrchr(fSegments.ItemAt(index + 1)->path.String(),
			'/');
		if (next != NULL)
			current = next + 1;
	}

	for (size_t i = 0; i < names.size(); i++) {
		BString path(segment->path);
		if (path != "/")
			path << '/';
		path << names[i];
		BMessage* message = new BMessage(kMsgGoPath);
		message->AddString("path", path);
		BMenuItem* item = new BMenuItem(names[i].String(), message);
		item->SetMarked(names[i] == current);
		menu->AddItem(item);
	}
	if (names.empty()) {
		BMenuItem* item = new BMenuItem(B_TRANSLATE("No folders"), NULL);
		item->SetEnabled(false);
		menu->AddItem(item);
	}
	menu->SetTargetForItems(this);

	BPoint where(segment->arrowFrame.LeftBottom());
	ConvertToScreen(&where);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, true, true);
}


void
BreadcrumbView::_ShowHiddenMenu()
{
	BPopUpMenu* menu = new BPopUpMenu("hidden", false, false);
	for (int32 i = fFirstVisible - 1; i >= 0; i--) {
		Segment* segment = fSegments.ItemAt(i);
		BMessage* message = new BMessage(kMsgGoPath);
		message->AddString("path", segment->path);
		menu->AddItem(new BMenuItem(segment->label.String(), message));
	}
	menu->SetTargetForItems(this);

	BPoint where(fEllipsisFrame.LeftBottom());
	ConvertToScreen(&where);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, true, true);
}


void
BreadcrumbView::MessageReceived(BMessage* message)
{
	if (message->what == kMsgGoPath) {
		_Go(message->GetString("path", "/"));
		return;
	}

	if (message->WasDropped() && message->HasRef("refs")) {
		// items dropped on a folder go there: moved on the same volume,
		// copied to another
		BPoint where = message->DropPoint();
		ConvertFromScreen(&where);
		bool onArrow;
		int32 index = _SegmentAt(where, &onArrow);
		fDropping = false;
		fHover = -1;
		Invalidate();
		if (index < 0)
			return;

		BEntry destEntry(fSegments.ItemAt(index)->path.String());
		struct stat destStat;
		if (destEntry.GetStat(&destStat) != B_OK)
			return;

		BObjectList<entry_ref, true>* list
			= new BObjectList<entry_ref, true>(10);
		bool sameVolume = true;
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
			list->AddItem(new entry_ref(ref));
			if (ref.device() != destStat.st_dev)
				sameVolume = false;
		}
		uint32 mode = sameVolume ? kMoveSelectionTo : kCopySelectionTo;
		if ((modifiers() & B_OPTION_KEY) != 0)
			mode = kCopySelectionTo;
		FSMoveToFolder(list, new BEntry(destEntry), mode, NULL);
		return;
	}

	BView::MessageReceived(message);
}


} // namespace BPrivate
