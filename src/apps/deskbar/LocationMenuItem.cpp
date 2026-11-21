/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "LocationMenuItem.h"

#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Menu.h>
#include <Messenger.h>
#include <NaturalCompare.h>
#include <Path.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <vector>

#include "FSUtils.h"
#include "IconCache.h"
#include "tracker_private.h"
#include "Utilities.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WindowMenu"


using namespace BPrivate;


static float sHPad, sVPad, sLabelOffset = 0.0f;


TLocationMenuItem::TLocationMenuItem(const BEntry& entry, const char* label)
	:
	BMenuItem("", NULL),
	fModel(&entry),
	fLabelWidth(0.0f),
	fLabelAscent(0.0f),
	fLabelDescent(0.0f)
{
	if (sHPad == 0.0f) {
		// the same padding as TWindowMenuItem, so the labels line up
		sHPad = be_control_look->ComposeSpacing(B_USE_ITEM_SPACING) - 1.0f;
		sVPad = ceilf(be_control_look->ComposeSpacing(B_USE_SMALL_SPACING)
			/ 4.0f);
		sLabelOffset = ceilf((be_control_look->DefaultLabelSpacing() / 3.0f)
			* 4.0f);
	}

	if (label == NULL)
		label = fModel.InitCheck() == B_OK ? fModel.Name() : "";

	// use menu font (parent font not available yet)
	menu_info info;
	get_menu_info(&info);
	BFont font;
	font.SetFamilyAndStyle(info.f_family, info.f_style);
	font.SetSize(info.font_size);
	fLabelWidth = ceilf(font.StringWidth(label));
	font_height fontHeight;
	font.GetHeight(&fontHeight);
	fLabelAscent = ceilf(fontHeight.ascent);
	fLabelDescent = ceilf(fontHeight.descent + fontHeight.leading);

	SetLabel(label);
}


status_t
TLocationMenuItem::InitCheck() const
{
	return fModel.InitCheck();
}


/*!	Adds the places the desktop shows, in its order: Home, the boot volume,
	Trash, then the other mounted volumes by name.
*/
/*static*/ void
TLocationMenuItem::AddLocations(BMenu* menu)
{
	BPath path;
	BEntry entry;
	if (find_directory(B_USER_DIRECTORY, &path) == B_OK
		&& entry.SetTo(path.Path()) == B_OK) {
		TLocationMenuItem* item
			= new TLocationMenuItem(entry, B_TRANSLATE("Home"));
		if (item->InitCheck() == B_OK)
			menu->AddItem(item);
		else
			delete item;
	}

	BVolumeRoster roster;
	BVolume bootVolume;
	roster.GetBootVolume(&bootVolume);

	BDirectory root;
	if (bootVolume.InitCheck() == B_OK
		&& bootVolume.GetRootDirectory(&root) == B_OK
		&& root.GetEntry(&entry) == B_OK) {
		TLocationMenuItem* item = new TLocationMenuItem(entry);
		if (item->InitCheck() == B_OK)
			menu->AddItem(item);
		else
			delete item;
	}

	BDirectory trash;
	if (bootVolume.InitCheck() == B_OK
		&& FSGetTrashDir(&trash, bootVolume.Device()) == B_OK
		&& trash.GetEntry(&entry) == B_OK) {
		TLocationMenuItem* item
			= new TLocationMenuItem(entry, B_TRANSLATE("Trash"));
		if (item->InitCheck() == B_OK)
			menu->AddItem(item);
		else
			delete item;
	}

	// the other volumes the desktop shows (see BPoseView::CreateVolumePose()):
	// persistent ones (no devfs and friends), network shares included, but
	// none mounted inside another persistent volume (packagefs at
	// /boot/system, for one)
	std::vector<TLocationMenuItem*> volumes;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK) {
		entry_ref ref;
		if (volume == bootVolume || !volume.IsPersistent()
			|| volume.GetRootDirectory(&root) != B_OK
			|| root.GetEntry(&entry) != B_OK
			|| entry.GetRef(&ref) != B_OK) {
			continue;
		}

		BVolume parentVolume(ref.device());
		if (parentVolume.InitCheck() == B_OK && parentVolume.IsPersistent())
			continue;

		TLocationMenuItem* item = new TLocationMenuItem(entry);
		if (item->InitCheck() != B_OK) {
			delete item;
			continue;
		}

		std::vector<TLocationMenuItem*>::iterator it = volumes.begin();
		while (it != volumes.end()
			&& NaturalCompare((*it)->Label(), item->Label()) <= 0) {
			it++;
		}
		volumes.insert(it, item);
	}

	for (size_t i = 0; i < volumes.size(); i++)
		menu->AddItem(volumes[i]);
}


void
TLocationMenuItem::GetContentSize(float* width, float* height)
{
	const float iconSize = ListIconSize();

	if (width != NULL)
		*width = sHPad + (iconSize - 1) + sLabelOffset + fLabelWidth + sHPad;

	if (height != NULL) {
		float labelHeight = fLabelAscent + fLabelDescent;
		*height = labelHeight > iconSize - 1 ? labelHeight : iconSize - 1;
		*height += sVPad * 2;
	}
}


void
TLocationMenuItem::DrawContent()
{
	BMenu* menu = Menu();
	const float iconSize = ListIconSize();

	float height;
	GetContentSize(NULL, &height);

	BPoint where = ContentLocation() + BPoint(sHPad, 0);
	where.y += floorf((height - (iconSize - 1)) / 2);

	menu->PushState();
	menu->SetDrawingMode(B_OP_OVER);
	menu->SetLowColor(B_TRANSPARENT_32_BIT);
	IconCache::sIconCache->Draw(fModel.ResolveIfLink(), menu, where,
		kNormalIcon, BSize(iconSize - 1, iconSize - 1));
	menu->PopState();

	BPoint labelLocation(ContentLocation().x + sHPad + (iconSize - 1)
		+ sLabelOffset, ContentLocation().y + sVPad + fLabelAscent);
	menu->SetDrawingMode(B_OP_COPY);
	menu->MovePenTo(labelLocation);

	if (IsSelected())
		menu->SetHighColor(ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR));
	else
		menu->SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));

	menu->DrawString(Label());
}


status_t
TLocationMenuItem::Invoke(BMessage* /*message*/)
{
	BMessage refs(B_REFS_RECEIVED);
	refs.AddRef("refs", fModel.EntryRef());
	refs.AddBool(kTrackerOpenOnMainDisplay, true);

	return BMessenger(kTrackerSignature).SendMessage(&refs);
}
