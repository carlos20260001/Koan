/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "Tags.h"

#include <Application.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <View.h>
#include <Volume.h>

#include <fs_index.h>
#include <string.h>

#include <algorithm>

#include "Attributes.h"
#include "FindPanel.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "MimeTypes.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Tags"


namespace BPrivate {


static BObjectList<TagInfo, true>* sTags;


static void
LoadTags()
{
	if (sTags != NULL)
		return;

	sTags = new BObjectList<TagInfo, true>(8);

	// ~/config/settings/Tracker/Tags: a message with "name" and "color"
	BPath path;
	if (FSFindTrackerSettingsDir(&path, false) == B_OK
		&& path.Append("Tags") == B_OK) {
		BFile file(path.Path(), B_READ_ONLY);
		BMessage settings;
		if (file.InitCheck() == B_OK && settings.Unflatten(&file) == B_OK) {
			const char* name;
			for (int32 i = 0; settings.FindString("name", i, &name) == B_OK;
					i++) {
				const void* data;
				ssize_t size;
				TagInfo* info = new TagInfo;
				info->name = name;
				info->color = make_color(128, 128, 128);
				if (settings.FindData("color", B_RGB_COLOR_TYPE, i, &data,
						&size) == B_OK && size == sizeof(rgb_color)) {
					info->color = *(const rgb_color*)data;
				}
				sTags->AddItem(info);
			}
		}
	}

	if (!sTags->IsEmpty())
		return;

	struct {
		const char*	name;
		rgb_color	color;
	} defaults[] = {
		{ B_TRANSLATE_MARK("Red"), { 230, 60, 50, 255 } },
		{ B_TRANSLATE_MARK("Orange"), { 245, 150, 30, 255 } },
		{ B_TRANSLATE_MARK("Yellow"), { 240, 200, 20, 255 } },
		{ B_TRANSLATE_MARK("Green"), { 60, 180, 80, 255 } },
		{ B_TRANSLATE_MARK("Blue"), { 40, 120, 230, 255 } },
		{ B_TRANSLATE_MARK("Purple"), { 160, 80, 210, 255 } },
		{ B_TRANSLATE_MARK("Gray"), { 140, 140, 145, 255 } }
	};
	for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
		TagInfo* info = new TagInfo;
		info->name = B_TRANSLATE_NOCOLLECT(defaults[i].name);
		info->color = defaults[i].color;
		sTags->AddItem(info);
	}
}


const BObjectList<TagInfo, true>&
TrackerTags()
{
	LoadTags();
	return *sTags;
}


rgb_color
TagColor(const char* name)
{
	LoadTags();
	for (int32 i = 0; i < sTags->CountItems(); i++) {
		if (sTags->ItemAt(i)->name.ICompare(name) == 0)
			return sTags->ItemAt(i)->color;
	}
	return make_color(140, 140, 145);
}


void
ParseTags(const char* value, BStringList& tags)
{
	tags.MakeEmpty();
	BString string(value);
	string.Split(",", true, tags);
	for (int32 i = tags.CountStrings() - 1; i >= 0; i--) {
		BString tag(tags.StringAt(i));
		tag.Trim();
		if (tag.Length() == 0)
			tags.Remove(i);
		else
			tags.Replace(i, tag);
	}
}


BString
JoinTags(const BStringList& tags)
{
	return tags.Join(",");
}


status_t
SetTag(const entry_ref& ref, const char* tag, bool on)
{
	BNode node(&ref);
	status_t status = node.InitCheck();
	if (status != B_OK)
		return status;

	// an index makes the tag findable with a query
	BVolume volume(ref.device());
	if (volume.KnowsQuery()) {
		struct index_info info;
		if (fs_stat_index(ref.device(), kAttrTags, &info) != 0)
			fs_create_index(ref.device(), kAttrTags, B_STRING_TYPE, 0);
	}

	BString value;
	node.ReadAttrString(kAttrTags, &value);
	BStringList tags;
	ParseTags(value.String(), tags);

	int32 found = -1;
	for (int32 i = 0; i < tags.CountStrings(); i++) {
		if (tags.StringAt(i).ICompare(tag) == 0) {
			found = i;
			break;
		}
	}

	if (on && found < 0)
		tags.Add(tag);
	else if (!on && found >= 0)
		tags.Remove(found);
	else
		return B_OK;

	if (tags.IsEmpty())
		return node.RemoveAttr(kAttrTags);

	BString joined(JoinTags(tags));
	return node.WriteAttrString(kAttrTags, &joined);
}


float
TagDotsWidth(const BStringList& tags, float dotSize)
{
	int32 count = tags.CountStrings();
	if (count == 0)
		return 0;
	return dotSize + (count - 1) * dotSize * 0.6f;
}


void
DrawTagDots(BView* view, float left, float centerY, const BStringList& tags,
	float dotSize)
{
	int32 count = tags.CountStrings();
	if (count == 0)
		return;

	float x = left;
	float top = floorf(centerY - dotSize / 2);

	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	rgb_color outline = view->LowColor();
	for (int32 i = 0; i < count; i++) {
		BRect dot(x, top, x + dotSize - 1, top + dotSize - 1);
		view->SetHighColor(outline);
		view->FillEllipse(dot.InsetByCopy(-1, -1));
		view->SetHighColor(TagColor(tags.StringAt(i).String()));
		view->FillEllipse(dot);
		x += dotSize * 0.6f;
	}
	view->PopState();
}


BBitmap*
TagIcon(rgb_color color, float size)
{
	BRect rect(0, 0, size - 1, size - 1);
	BBitmap* bitmap = new BBitmap(rect, B_RGBA32, true);
	BView* view = new BView(rect, "dot", 0, 0);
	bitmap->AddChild(view);
	if (!bitmap->Lock())
		return bitmap;

	view->SetHighColor(0, 0, 0, 0);
	view->FillRect(rect);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	view->SetHighColor(color);
	view->FillEllipse(rect.InsetByCopy(size / 6, size / 6));
	view->Sync();
	bitmap->Unlock();
	return bitmap;
}


void
BuildTagsMenu(BMenu* menu, const BObjectList<entry_ref, true>& refs,
	BHandler* target)
{
	// which tags every item has
	BStringList common;
	bool first = true;
	for (int32 i = 0; i < refs.CountItems(); i++) {
		BNode node(refs.ItemAt(i));
		BString value;
		node.ReadAttrString(kAttrTags, &value);
		BStringList tags;
		ParseTags(value.String(), tags);
		if (first) {
			common = tags;
			first = false;
			continue;
		}
		for (int32 j = common.CountStrings() - 1; j >= 0; j--) {
			if (!tags.HasString(common.StringAt(j), true))
				common.Remove(j);
		}
	}

	float iconSize = be_control_look->ComposeIconSize(B_MINI_ICON).Width() + 1;
	const BObjectList<TagInfo, true>& allTags = TrackerTags();
	for (int32 i = 0; i < allTags.CountItems(); i++) {
		TagInfo* info = allTags.ItemAt(i);
		BMessage* message = new BMessage(kToggleTag);
		message->AddString("tag", info->name);
		BBitmap* icon = TagIcon(info->color, iconSize);
		IconMenuItem* item = new IconMenuItem(info->name.String(), message,
			icon);
		delete icon;
			// the item keeps a copy
		item->SetMarked(!refs.IsEmpty() && common.HasString(info->name, true));
		item->SetEnabled(!refs.IsEmpty());
		item->SetTarget(target);
		menu->AddItem(item);
	}

	menu->AddSeparatorItem();
	BMenu* findMenu = new BMenu(B_TRANSLATE("Find items tagged"));
	for (int32 i = 0; i < allTags.CountItems(); i++) {
		TagInfo* info = allTags.ItemAt(i);
		BMessage* message = new BMessage(kFindTagged);
		message->AddString("tag", info->name);
		BBitmap* icon = TagIcon(info->color, iconSize);
		IconMenuItem* item = new IconMenuItem(info->name.String(), message,
			icon);
		delete icon;
		item->SetTarget(target);
		findMenu->AddItem(item);
	}
	menu->AddItem(findMenu);
}


void
FindTaggedItems(const char* tag)
{
	BPath queries;
	if (find_directory(B_USER_DIRECTORY, &queries, true) != B_OK
		|| queries.Append("queries") != B_OK) {
		return;
	}
	create_directory(queries.Path(), 0755);

	BString name(B_TRANSLATE_COMMENT("Tagged %tag",
		"Name of the query that finds the items with a tag"));
	name.ReplaceFirst("%tag", tag);
	name.ReplaceAll("/", "-");
	BPath path(queries);
	path.Append(name.String());

	BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;

	BNodeInfo(&file).SetType(B_QUERY_MIMETYPE);
	// the tag alone, first, last or in between in "Red,Work,Blue"
	BString predicate;
	predicate << "((" << kAttrTags << "==\"" << tag << "\")||("
		<< kAttrTags << "==\"" << tag << ",*\")||("
		<< kAttrTags << "==\"*," << tag << "\")||("
		<< kAttrTags << "==\"*," << tag << ",*\"))";
	file.WriteAttrString(kAttrQueryString, &predicate);

	MoreOptionsStruct options;
	options.temporary = true;
	file.WriteAttr(kAttrQueryMoreOptions, B_RAW_TYPE, 0, &options,
		sizeof(options));
	file.Unset();

	entry_ref ref;
	if (get_ref_for_path(path.Path(), &ref) != B_OK)
		return;

	BMessage open(B_REFS_RECEIVED);
	open.AddRef("refs", &ref);
	be_app->PostMessage(&open);
}


} // namespace BPrivate
