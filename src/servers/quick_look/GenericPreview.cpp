/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "GenericPreview.h"

#include <algorithm>
#include <stdio.h>
#include <time.h>

#include <Bitmap.h>
#include <Catalog.h>
#include <Directory.h>
#include <Font.h>
#include <Locale.h>
#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <StringFormat.h>
#include <SymLink.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "QuickLook"


static const float kIconSize = 128;


static BString
format_size(off_t size)
{
	static const char* units[] = {"bytes", "KiB", "MiB", "GiB", "TiB"};
	double value = size;
	int unit = 0;
	while (value >= 1024 && unit < 4) {
		value /= 1024;
		unit++;
	}
	BString text;
	if (unit == 0)
		text.SetToFormat("%" B_PRIdOFF " %s", size, units[0]);
	else
		text.SetToFormat("%.1f %s", value, units[unit]);
	return text;
}


GenericPreview::GenericPreview(const entry_ref& ref, const char* mimeType,
	const char* note)
	:
	BQuickLookPreview("generic preview"),
	fIcon(NULL),
	fNote(note)
{
	SetViewColor(46, 46, 46);
	SetLowColor(ViewColor());
	_Describe(ref, mimeType);
}


GenericPreview::~GenericPreview()
{
	delete fIcon;
}


BSize
GenericPreview::PreferredContentSize()
{
	float width = std::max(440.0f, be_plain_font->StringWidth(fName) + 80);
	return BSize(std::min(width, 720.0f), 300);
}


BString
GenericPreview::Summary()
{
	BString summary = fKind;
	if (fDetail.Length() > 0)
		summary << " \xE2\x80\xA2 " << fDetail;
	return summary;
}


void
GenericPreview::AttachedToWindow()
{
	BQuickLookPreview::AttachedToWindow();
}


void
GenericPreview::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	float iconScale = ceilf(kIconSize);
	BRect icon(0, 0, iconScale - 1, iconScale - 1);
	icon.OffsetTo(floorf((bounds.Width() - iconScale) / 2),
		floorf(bounds.Height() * 0.18f));
	if (fIcon != NULL) {
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(fIcon, fIcon->Bounds(), icon, B_FILTER_BITMAP_BILINEAR);
		SetDrawingMode(B_OP_COPY);
	}

	BFont font(be_bold_font);
	font.SetSize(be_plain_font->Size() * 1.3f);
	font_height height;
	font.GetHeight(&height);
	SetFont(&font);
	SetHighColor(240, 240, 240);
	float y = icon.bottom + 24 + height.ascent;
	BString name(fName);
	TruncateString(&name, B_TRUNCATE_MIDDLE, bounds.Width() - 40);
	DrawString(name, BPoint(floorf((bounds.Width() - StringWidth(name)) / 2),
		y));
	y += height.descent + 8;

	SetFont(be_plain_font);
	be_plain_font->GetHeight(&height);
	SetHighColor(180, 180, 180);
	BString line(fKind);
	if (fDate.Length() > 0)
		line << " \xE2\x80\x94 " << fDate;
	TruncateString(&line, B_TRUNCATE_END, bounds.Width() - 40);
	y += height.ascent;
	DrawString(line, BPoint(floorf((bounds.Width() - StringWidth(line)) / 2),
		y));
	if (fNote.Length() > 0) {
		y += height.descent + height.leading + height.ascent + 4;
		SetHighColor(150, 150, 150);
		BString note(fNote);
		TruncateString(&note, B_TRUNCATE_END, bounds.Width() - 40);
		DrawString(note, BPoint(floorf((bounds.Width() - StringWidth(note))
			/ 2), y));
	}
}


void
GenericPreview::_Describe(const entry_ref& ref, const char* mimeType)
{
	fName = ref.name;

	BEntry entry(&ref, true);
	struct stat st;
	bool haveStat = entry.GetStat(&st) == B_OK;

	fIcon = new BBitmap(BRect(0, 0, kIconSize - 1, kIconSize - 1), B_RGBA32);
	if (BNodeInfo::GetTrackerIcon(&ref, fIcon, (icon_size)(int)kIconSize)
			!= B_OK) {
		delete fIcon;
		fIcon = NULL;
	}

	BMimeType type(mimeType);
	char description[B_MIME_TYPE_LENGTH];
	if (type.GetShortDescription(description) == B_OK)
		fKind = description;
	else
		fKind = mimeType;

	if (entry.IsDirectory()) {
		fKind = B_TRANSLATE("Folder");
		BDirectory directory(&entry);
		int32 count = directory.CountEntries();
		static BStringFormat format(B_TRANSLATE("{0, plural, "
			"=0{no items} =1{one item} other{# items}}"));
		format.Format(fDetail, count);
	} else if (haveStat) {
		fDetail = format_size(st.st_size);
	}

	if (haveStat) {
		char buffer[64];
		struct tm local;
		localtime_r(&st.st_mtime, &local);
		strftime(buffer, sizeof(buffer), "%x %H:%M", &local);
		fDate = buffer;
	}
}
