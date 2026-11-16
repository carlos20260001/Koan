/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "DetailsPane.h"

#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <DateTimeFormat.h>
#include <GridLayout.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MimeType.h>
#include <Node.h>
#include <Path.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <StringForSize.h>
#include <StringFormat.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <fs_attr.h>
#include <stdio.h>
#include <string.h>

#include <set>

#include "Commands.h"
#include "FolderSizes.h"
#include "IconCache.h"
#include "Model.h"
#include "Pose.h"
#include "PoseView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DetailsPane"


namespace BPrivate {


static const uint32 kMsgAttributeEdited = 'DPae';
static const float kPictureSize = 128;


class PictureView : public BView {
public:
	PictureView()
		:
		BView("picture", B_WILL_DRAW),
		fModel(NULL)
	{
		float size = be_control_look->ComposeIconSize((int32)kPictureSize)
			.Width() + 1;
		SetExplicitSize(BSize(B_SIZE_UNSET, size + 8));
		SetExplicitMinSize(BSize(size, size + 8));
	}

	void SetModel(Model* model)
	{
		fModel = model;
		Invalidate();
	}

	virtual void AttachedToWindow()
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	virtual void Draw(BRect updateRect)
	{
		if (fModel == NULL)
			return;

		BSize size = be_control_look->ComposeIconSize((int32)kPictureSize);
		BPoint where((Bounds().Width() - size.Width()) / 2, 4);
		SetDrawingMode(B_OP_ALPHA);
		IconCache::sIconCache->Draw(fModel, this, where, kNormalIcon, size,
			true);
	}

private:
	Model*	fModel;
};


DetailsPane::DetailsPane()
	:
	BView("details", B_WILL_DRAW),
	fShown(10),
	fModel(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	// the content says how narrow it can be; this is how wide it starts
	SetExplicitPreferredSize(BSize(be_plain_font->StringWidth("M") * 20,
		B_SIZE_UNSET));
	// a long list of attributes is cut off rather than making the window
	// taller
	SetExplicitMinSize(BSize(B_SIZE_UNSET, 60));

	BGroupLayout* layout = new BGroupLayout(B_VERTICAL, 0);
	SetLayout(layout);

	BView* content = new BView("content", 0);
	content->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fContent = new BGroupLayout(B_VERTICAL, B_USE_HALF_ITEM_SPACING);
	content->SetLayout(fContent);
	fContentView = content;
	fContent->SetInsets(B_USE_HALF_ITEM_INSETS);

	fIconView = new PictureView();
	fContent->AddView(fIconView);

	layout->AddView(content);
	fFolder.unset();
}


DetailsPane::~DetailsPane()
{
	delete fModel;
}


void
DetailsPane::_Clear()
{
	// everything but the picture: the views, then glue and struts
	while (BView* child = fContentView->ChildAt(1)) {
		child->RemoveSelf();
		delete child;
	}
	while (fContent->CountItems() > 1)
		delete fContent->RemoveItem(1);
}


void
DetailsPane::_AddRow(const char* label, const char* value)
{
	if (value == NULL || value[0] == '\0')
		return;

	BStringView* labelView = new BStringView("label", label);
	BFont font(be_plain_font);
	font.SetSize(be_plain_font->Size() * 0.9f);
	labelView->SetFont(&font);
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	labelView->SetHighColor(tint_color(text, text.IsDark()
		? B_LIGHTEN_1_TINT : B_DARKEN_1_TINT));

	BStringView* valueView = new BStringView("value", value);
	valueView->SetTruncation(B_TRUNCATE_MIDDLE);
	valueView->SetExplicitMinSize(BSize(10, B_SIZE_UNSET));
	valueView->SetToolTip(value);

	fContent->AddItem(BSpaceLayoutItem::CreateVerticalStrut(2));
	fContent->AddView(labelView);
	fContent->AddView(valueView);
}


static BString
FormatAttribute(BNode& node, const char* name, uint32 type)
{
	BString result;
	attr_info info;
	if (node.GetAttrInfo(name, &info) != B_OK)
		return result;

	switch (type) {
		case B_STRING_TYPE:
		case B_MIME_STRING_TYPE:
			node.ReadAttrString(name, &result);
			break;
		case B_INT8_TYPE: case B_UINT8_TYPE: case B_INT16_TYPE:
		case B_UINT16_TYPE: case B_INT32_TYPE: case B_UINT32_TYPE:
		case B_INT64_TYPE: case B_UINT64_TYPE: case B_OFF_T_TYPE:
		{
			int64 value = 0;
			char buffer[8] = {};
			ssize_t size = std::min((off_t)8, info.size);
			if (node.ReadAttr(name, type, 0, buffer, size) == size) {
				switch (size) {
					case 1: value = *(int8*)buffer; break;
					case 2: value = *(int16*)buffer; break;
					case 4: value = *(int32*)buffer; break;
					default: value = *(int64*)buffer; break;
				}
				result << value;
			}
			break;
		}
		case B_FLOAT_TYPE:
		{
			float value;
			if (node.ReadAttr(name, type, 0, &value, sizeof(value))
					== sizeof(value)) {
				result.SetToFormat("%g", value);
			}
			break;
		}
		case B_DOUBLE_TYPE:
		{
			double value;
			if (node.ReadAttr(name, type, 0, &value, sizeof(value))
					== sizeof(value)) {
				result.SetToFormat("%g", value);
			}
			break;
		}
		case B_BOOL_TYPE:
		{
			bool value;
			if (node.ReadAttr(name, type, 0, &value, sizeof(value))
					== sizeof(value)) {
				result = value ? B_TRANSLATE("Yes") : B_TRANSLATE("No");
			}
			break;
		}
		case B_TIME_TYPE:
		{
			time_t value = 0;
			ssize_t size = std::min((off_t)sizeof(time_t), info.size);
			if (node.ReadAttr(name, type, 0, &value, size) == size)
				BDateTimeFormat().Format(result, value, B_MEDIUM_DATE_FORMAT,
					B_SHORT_TIME_FORMAT);
			break;
		}
	}
	return result;
}


void
DetailsPane::_AddAttributes(Model* model)
{
	// the attributes the type shows in its columns, editable strings as
	// fields
	BMimeType mimeType(model->MimeType());
	BMessage info;
	if (mimeType.InitCheck() != B_OK || mimeType.GetAttrInfo(&info) != B_OK)
		return;

	BNode node(model->EntryRef());
	if (node.InitCheck() != B_OK)
		return;

	const char* name;
	for (int32 i = 0; info.FindString("attr:name", i, &name) == B_OK; i++) {
		if (!info.GetBool("attr:viewable", i, true))
			continue;

		const char* label = info.GetString("attr:public_name", i, name);
		uint32 type = (uint32)info.GetInt32("attr:type", i, B_STRING_TYPE);
		bool editable = info.GetBool("attr:editable", i, false)
			&& type == B_STRING_TYPE;
		BString value = FormatAttribute(node, name, type);

		if (editable) {
			BMessage* message = new BMessage(kMsgAttributeEdited);
			message->AddString("attr", name);
			BTextControl* control = new BTextControl(label, value.String(),
				message);
			control->SetTarget(this);
			BStringView* labelView = new BStringView("label", label);
			BFont font(be_plain_font);
			font.SetSize(be_plain_font->Size() * 0.9f);
			labelView->SetFont(&font);
			control->SetLabel(NULL);
			fContent->AddView(labelView);
			fContent->AddView(control);
		} else
			_AddRow(label, value.String());
	}
}


void
DetailsPane::Update(BPoseView* poseView, bool force)
{
	// only when what is shown changed
	PoseList* selection = poseView->SelectionList();
	bool same = !force && selection->CountItems() == fShown.CountItems()
		&& poseView->TargetModel() != NULL
		&& *poseView->TargetModel()->EntryRef() == fFolder;
	for (int32 i = 0; same && i < selection->CountItems(); i++) {
		if (*selection->ItemAt(i)->TargetModel()->EntryRef()
				!= *fShown.ItemAt(i)) {
			same = false;
		}
	}
	if (same)
		return;

	fShown.MakeEmpty();
	for (int32 i = 0; i < selection->CountItems(); i++) {
		fShown.AddItem(new entry_ref(
			*selection->ItemAt(i)->TargetModel()->EntryRef()));
	}
	if (poseView->TargetModel() != NULL)
		fFolder = *poseView->TargetModel()->EntryRef();

	_Rebuild(poseView);
}


void
DetailsPane::_Rebuild(BPoseView* poseView)
{
	_Clear();
	((PictureView*)fIconView)->SetModel(NULL);
	delete fModel;
	fModel = NULL;

	char sizeText[128];
	int32 count = fShown.CountItems();

	if (count > 1) {
		// several: how many, how big, which kinds
		static BStringFormat countFormat(B_TRANSLATE_COMMENT(
			"{0, plural, other{# items}}", "Several selected items"));
		BString title;
		countFormat.Format(title, count);
		BStringView* titleView = new BStringView("title", title.String());
		titleView->SetFont(be_bold_font);
		fContent->AddView(titleView);

		off_t total = 0;
		std::set<BString> kinds;
		PoseList* selection = poseView->SelectionList();
		for (int32 i = 0; i < selection->CountItems(); i++) {
			Model* model = selection->ItemAt(i)->TargetModel();
			if (model->IsFile())
				total += model->StatBuf()->st_size;
			else if (model->IsDirectory()) {
				off_t size;
				if (GetFolderSize(*model->NodeRef(), &size))
					total += size;
			}
			BMimeType type(model->MimeType());
			char description[B_MIME_TYPE_LENGTH];
			if (type.GetShortDescription(description) == B_OK)
				kinds.insert(description);
		}
		string_for_size(total, sizeText, sizeof(sizeText));
		_AddRow(B_TRANSLATE("Size"), sizeText);

		BString kindList;
		int32 shown = 0;
		for (std::set<BString>::iterator it = kinds.begin();
				it != kinds.end() && shown < 4; it++, shown++) {
			if (kindList.Length() > 0)
				kindList << ", ";
			kindList << *it;
		}
		if ((int32)kinds.size() > shown)
			kindList << ", " B_UTF8_ELLIPSIS;
		_AddRow(B_TRANSLATE("Kinds"), kindList.String());
		fContent->AddItem(BSpaceLayoutItem::CreateGlue());
		return;
	}

	// one item, or the folder itself
	entry_ref ref = count == 1 ? *fShown.ItemAt(0) : fFolder;
	fModel = new Model(&ref, true);
	if (fModel->InitCheck() != B_OK) {
		delete fModel;
		fModel = NULL;
		fContent->AddItem(BSpaceLayoutItem::CreateGlue());
		return;
	}
	((PictureView*)fIconView)->SetModel(fModel);

	BStringView* nameView = new BStringView("name", fModel->Name());
	nameView->SetFont(be_bold_font);
	nameView->SetTruncation(B_TRUNCATE_MIDDLE);
	nameView->SetExplicitMinSize(BSize(10, B_SIZE_UNSET));
	nameView->SetAlignment(B_ALIGN_CENTER);
	nameView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fContent->AddView(nameView);

	if (count == 1) {
		BButton* open = new BButton(B_TRANSLATE("Open"),
			new BMessage(kOpenSelection));
		BButton* look = new BButton(B_TRANSLATE("Quick Look"),
			new BMessage(kQuickLook));
		open->SetTarget(poseView);
		look->SetTarget(poseView);
		BGroupView* buttons = new BGroupView(B_HORIZONTAL);
		buttons->GroupLayout()->AddView(open);
		buttons->GroupLayout()->AddView(look);
		fContent->AddView(buttons);
	}


	BMimeType mimeType(fModel->MimeType());
	char description[B_MIME_TYPE_LENGTH];
	if (mimeType.GetShortDescription(description) == B_OK)
		_AddRow(B_TRANSLATE("Kind"), description);
	else
		_AddRow(B_TRANSLATE("Kind"), fModel->MimeType());

	const struct stat* st = fModel->StatBuf();
	if (fModel->IsFile()) {
		string_for_size(st->st_size, sizeText, sizeof(sizeText));
		_AddRow(B_TRANSLATE("Size"), sizeText);
	} else if (fModel->IsDirectory()) {
		off_t size;
		if (GetFolderSize(*fModel->NodeRef(), &size)) {
			string_for_size(size, sizeText, sizeof(sizeText));
			_AddRow(B_TRANSLATE("Size"), sizeText);
		}
	}

	BDateTimeFormat format;
	BString date;
	format.Format(date, st->st_mtime, B_MEDIUM_DATE_FORMAT,
		B_SHORT_TIME_FORMAT);
	_AddRow(B_TRANSLATE("Modified"), date.String());
	date = "";
	format.Format(date, fModel->CreationTime(), B_MEDIUM_DATE_FORMAT,
		B_SHORT_TIME_FORMAT);
	_AddRow(B_TRANSLATE("Created"), date.String());

	BPath path(&ref);
	BPath parent;
	if (path.GetParent(&parent) == B_OK)
		_AddRow(B_TRANSLATE("Where"), parent.Path());

	_AddAttributes(fModel);

	fContent->AddItem(BSpaceLayoutItem::CreateGlue());
}


void
DetailsPane::MessageReceived(BMessage* message)
{
	if (message->what != kMsgAttributeEdited) {
		BView::MessageReceived(message);
		return;
	}

	// an edited attribute is written at once
	const char* name = message->GetString("attr", NULL);
	BTextControl* control = NULL;
	void* source;
	if (message->FindPointer("source", &source) == B_OK)
		control = dynamic_cast<BTextControl*>((BArchivable*)source);
	if (name == NULL || control == NULL || fModel == NULL)
		return;

	BNode node(fModel->EntryRef());
	BString value(control->Text());
	if (value.Length() == 0)
		node.RemoveAttr(name);
	else
		node.WriteAttrString(name, &value);
}


} // namespace BPrivate
