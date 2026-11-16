/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "BatchRenameWindow.h"

#include <Alert.h>
#include <Button.h>
#include <CardLayout.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Directory.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringFormat.h>
#include <StringView.h>
#include <TextControl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <regex>
#include <set>
#include <string>

#include "ContainerWindow.h"
#include "FSUndoRedo.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BatchRenameWindow"


namespace BPrivate {


enum {
	kModeReplace = 0,
	kModeAdd,
	kModeNumber
};

static const uint32 kMsgModeChanged = 'brmd';
static const uint32 kMsgWhereChanged = 'brwh';
static const uint32 kMsgChanged = 'brch';
static const uint32 kMsgRename = 'brrn';


static void
SplitExtension(const char* name, BString& base, BString& extension)
{
	// "photo.tar.gz" keeps ".gz"; a leading dot is part of the name
	const char* dot = strrchr(name, '.');
	if (dot == NULL || dot == name) {
		base = name;
		extension = "";
		return;
	}

	base.SetTo(name, dot - name);
	extension = dot;
}


class PreviewItem : public BListItem {
public:
	PreviewItem(const char* oldName, const char* newName, bool clash)
		:
		fOldName(oldName),
		fNewName(newName),
		fClash(clash)
	{
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		rgb_color background = ui_color(B_LIST_BACKGROUND_COLOR);
		owner->SetLowColor(background);
		owner->FillRect(frame, B_SOLID_LOW);

		font_height fontHeight;
		owner->GetFontHeight(&fontHeight);
		float baseline = frame.top + ceilf(fontHeight.ascent) + 2;
		float half = frame.left + frame.Width() / 2;
		float spacing = be_control_look->DefaultLabelSpacing();

		BString oldName(fOldName);
		owner->TruncateString(&oldName, B_TRUNCATE_MIDDLE,
			half - frame.left - 3 * spacing);
		owner->SetHighUIColor(B_LIST_ITEM_TEXT_COLOR);
		owner->DrawString(oldName.String(), BPoint(frame.left + spacing,
			baseline));
		const char* arrow = "→";
		owner->DrawString(arrow, BPoint(half - spacing
			- owner->StringWidth(arrow), baseline));

		BString newName(fNewName);
		owner->TruncateString(&newName, B_TRUNCATE_MIDDLE,
			frame.right - half - spacing);
		if (fClash)
			owner->SetHighColor(ui_color(B_FAILURE_COLOR));
		owner->DrawString(newName.String(), BPoint(half, baseline));
	}

	virtual void Update(BView* owner, const BFont* font)
	{
		BListItem::Update(owner, font);
		SetHeight(Height() + 4);
	}

private:
	BString fOldName;
	BString fNewName;
	bool fClash;
};


BatchRenameWindow::BatchRenameWindow(BContainerWindow* parent,
	const BObjectList<entry_ref, true>& refs)
	:
	BWindow(BRect(0, 0, 520, 420), B_TRANSLATE("Rename"), B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE | B_NOT_ZOOMABLE),
	fParent(parent),
	fRefs(refs.CountItems()),
	fNewNames(refs.CountItems()),
	fMode(kModeReplace),
	fAddBefore(false)
{
	for (int32 index = 0; index < refs.CountItems(); index++)
		fRefs.AddItem(new entry_ref(*refs.ItemAt(index)));

	static BStringFormat titleFormat(B_TRANSLATE_COMMENT(
		"{0, plural, other{Rename # items}}", "Window title"));
	BString title;
	titleFormat.Format(title, fRefs.CountItems());
	SetTitle(title.String());

	// mode
	BPopUpMenu* modeMenu = new BPopUpMenu("mode");
	const char* modes[] = {
		B_TRANSLATE("Replace text"),
		B_TRANSLATE("Add text"),
		B_TRANSLATE("Number")
	};
	for (int32 i = 0; i < 3; i++) {
		BMessage* message = new BMessage(kMsgModeChanged);
		message->AddInt32("mode", i);
		BMenuItem* item = new BMenuItem(modes[i], message);
		modeMenu->AddItem(item);
		if (i == 0)
			item->SetMarked(true);
	}
	fModeField = new BMenuField("mode", B_TRANSLATE("Rename by:"), modeMenu);

	// replace
	fFindText = new BTextControl(B_TRANSLATE("Find:"), "",
		new BMessage(kMsgChanged));
	fReplaceText = new BTextControl(B_TRANSLATE("Replace with:"), "",
		new BMessage(kMsgChanged));
	fRegexCheckBox = new BCheckBox(B_TRANSLATE("Regular expression"),
		new BMessage(kMsgChanged));
	fCaseCheckBox = new BCheckBox(B_TRANSLATE("Match case"),
		new BMessage(kMsgChanged));
	fCaseCheckBox->SetValue(B_CONTROL_ON);

	// add
	fAddText = new BTextControl(B_TRANSLATE("Text:"), "",
		new BMessage(kMsgChanged));
	BPopUpMenu* whereMenu = new BPopUpMenu("where");
	BMessage* message = new BMessage(kMsgWhereChanged);
	message->AddBool("before", false);
	BMenuItem* afterItem = new BMenuItem(B_TRANSLATE("After the name"),
		message);
	afterItem->SetMarked(true);
	whereMenu->AddItem(afterItem);
	message = new BMessage(kMsgWhereChanged);
	message->AddBool("before", true);
	whereMenu->AddItem(new BMenuItem(B_TRANSLATE("Before the name"), message));
	fWhereField = new BMenuField("where", B_TRANSLATE("Where:"), whereMenu);

	// number
	fBaseName = new BTextControl(B_TRANSLATE("Name:"),
		B_TRANSLATE_COMMENT("Item", "Default base name for numbered items"),
		new BMessage(kMsgChanged));
	fStartAt = new BTextControl(B_TRANSLATE("Start at:"), "1",
		new BMessage(kMsgChanged));
	fDigits = new BTextControl(B_TRANSLATE("Digits:"), "2",
		new BMessage(kMsgChanged));

	BTextControl* textControls[] = { fFindText, fReplaceText, fAddText,
		fBaseName, fStartAt, fDigits };
	for (size_t i = 0; i < sizeof(textControls) / sizeof(textControls[0]); i++)
		textControls[i]->SetModificationMessage(new BMessage(kMsgChanged));

	fPreview = new BListView("preview", B_SINGLE_SELECTION_LIST);
	BScrollView* previewScroll = new BScrollView("preview scroll", fPreview,
		0, false, true);
	previewScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 160));

	fStatus = new BStringView("status", "");
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BButton* cancelButton = new BButton(B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));
	fRenameButton = new BButton(B_TRANSLATE("Rename"),
		new BMessage(kMsgRename));
	SetDefaultButton(fRenameButton);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGrid()
			.AddMenuField(fModeField, 0, 0)
		.End()
		.AddCards()
			.GetLayout(&fCards)
			.AddGrid()
				.AddTextControl(fFindText, 0, 0)
				.AddTextControl(fReplaceText, 0, 1)
				.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 1, 2)
					.Add(fRegexCheckBox)
					.Add(fCaseCheckBox)
					.AddGlue()
				.End()
			.End()
			.AddGrid()
				.AddTextControl(fAddText, 0, 0)
				.AddMenuField(fWhereField, 0, 1)
				.AddGlue(0, 2)
			.End()
			.AddGrid()
				.AddTextControl(fBaseName, 0, 0)
				.AddTextControl(fStartAt, 0, 1)
				.AddTextControl(fDigits, 0, 2)
			.End()
		.End()
		.Add(previewScroll, 1.0f)
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus)
			.AddGlue()
			.Add(cancelButton)
			.Add(fRenameButton)
		.End();

	fCards->SetVisibleItem((int32)0);

	AddToSubset(parent);
	CenterIn(parent->Frame());

	fFindText->MakeFocus(true);
	_UpdatePreview();
}


void
BatchRenameWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgModeChanged:
			fMode = message->GetInt32("mode", kModeReplace);
			fCards->SetVisibleItem(fMode);
			if (fMode == kModeReplace)
				fFindText->MakeFocus(true);
			else if (fMode == kModeAdd)
				fAddText->MakeFocus(true);
			else
				fBaseName->MakeFocus(true);
			_UpdatePreview();
			break;

		case kMsgWhereChanged:
			fAddBefore = message->GetBool("before", false);
			_UpdatePreview();
			break;

		case kMsgChanged:
			_UpdatePreview();
			break;

		case kMsgRename:
			_Rename();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


BString
BatchRenameWindow::_NewName(const char* name, int32 index, bool* error)
{
	BString base;
	BString extension;
	SplitExtension(name, base, extension);

	switch (fMode) {
		case kModeReplace:
		{
			const char* find = fFindText->Text();
			if (find[0] == '\0')
				return BString(name);

			if (fRegexCheckBox->Value() == B_CONTROL_ON) {
				try {
					std::regex::flag_type flags = std::regex::ECMAScript;
					if (fCaseCheckBox->Value() != B_CONTROL_ON)
						flags |= std::regex::icase;
					std::regex expression(find, flags);
					std::string result = std::regex_replace(std::string(name),
						expression, std::string(fReplaceText->Text()));
					return BString(result.c_str());
				} catch (...) {
					*error = true;
					return BString(name);
				}
			}

			BString result(name);
			if (fCaseCheckBox->Value() == B_CONTROL_ON)
				result.ReplaceAll(find, fReplaceText->Text());
			else
				result.IReplaceAll(find, fReplaceText->Text());
			return result;
		}

		case kModeAdd:
		{
			BString result;
			if (fAddBefore)
				result << fAddText->Text() << name;
			else
				result << base << fAddText->Text() << extension;
			return result;
		}

		case kModeNumber:
		default:
		{
			int32 start = atoi(fStartAt->Text());
			int32 digits = atoi(fDigits->Text());
			if (digits < 1)
				digits = 1;
			if (digits > 9)
				digits = 9;

			char number[32];
			snprintf(number, sizeof(number), "%0*" B_PRId32, (int)digits,
				start + index);

			BString result(fBaseName->Text());
			if (result.Length() > 0)
				result << ' ';
			result << number << extension;
			return result;
		}
	}
}


void
BatchRenameWindow::_UpdatePreview()
{
	fNewNames.MakeEmpty();

	bool regexError = false;
	for (int32 index = 0; index < fRefs.CountItems(); index++) {
		fNewNames.AddItem(new BString(_NewName(fRefs.ItemAt(index)->name,
			index, &regexError)));
	}

	// a new name clashes when it is empty, has a slash, is taken by another
	// new name, or by an item in the folder that is not being renamed
	std::set<std::string> renamed;
	for (int32 index = 0; index < fRefs.CountItems(); index++) {
		entry_ref* ref = fRefs.ItemAt(index);
		char key[B_FILE_NAME_LENGTH + 32];
		snprintf(key, sizeof(key), "%" B_PRIdINO "/%s", ref->directory(),
			ref->name);
		renamed.insert(key);
	}

	std::set<std::string> taken;
	int32 clashes = 0;
	int32 changes = 0;

	for (int32 index = 0; index < fPreview->CountItems(); index++)
		delete fPreview->ItemAt(index);
	fPreview->MakeEmpty();

	for (int32 index = 0; index < fRefs.CountItems(); index++) {
		entry_ref* ref = fRefs.ItemAt(index);
		BString* newName = fNewNames.ItemAt(index);

		char key[B_FILE_NAME_LENGTH + 32];
		snprintf(key, sizeof(key), "%" B_PRIdINO "/%s", ref->directory(),
			newName->String());

		bool clash = newName->Length() == 0
			|| newName->Length() >= B_FILE_NAME_LENGTH
			|| newName->FindFirst('/') >= 0 || *newName == "."
			|| *newName == ".." || taken.find(key) != taken.end();

		if (!clash && *newName != ref->name
			&& renamed.find(key) == renamed.end()) {
			entry_ref target = *ref;
			target.set_name(newName->String());
			clash = BEntry(&target).Exists();
		}

		taken.insert(key);
		if (clash)
			clashes++;
		if (*newName != ref->name)
			changes++;

		fPreview->AddItem(new PreviewItem(ref->name, newName->String(), clash));
	}

	BString status;
	if (regexError)
		status = B_TRANSLATE("The regular expression is not valid.");
	else if (clashes > 0) {
		static BStringFormat clashFormat(B_TRANSLATE_COMMENT(
			"{0, plural, one{One new name is taken or not allowed.}"
			" other{# new names are taken or not allowed.}}",
			"Shown under the preview of new names"));
		clashFormat.Format(status, clashes);
	}
	fStatus->SetText(status.String());
	fRenameButton->SetEnabled(!regexError && clashes == 0 && changes > 0);
}


void
BatchRenameWindow::_Rename()
{
	_UpdatePreview();
	if (!fRenameButton->IsEnabled())
		return;

	// Two steps, so names can move around within the set ("a"→"b" and
	// "b"→"c"): everything to a temporary name first, then to the new one.
	BObjectList<entry_ref, true> temporary(fRefs.CountItems());
	BObjectList<entry_ref, true> undoFrom(fRefs.CountItems());
	BObjectList<BString, true> undoTo(fRefs.CountItems());
	int32 failures = 0;

	for (int32 index = 0; index < fRefs.CountItems(); index++) {
		entry_ref* ref = fRefs.ItemAt(index);
		if (*fNewNames.ItemAt(index) == ref->name) {
			temporary.AddItem(NULL);
			continue;
		}

		char tempName[B_FILE_NAME_LENGTH];
		snprintf(tempName, sizeof(tempName), ".tracker-rename-%" B_PRId32
			"-%" B_PRIdINO, index, (ino_t)system_time());
		BEntry entry(ref);
		if (entry.Rename(tempName) == B_OK) {
			entry_ref tempRef;
			entry.GetRef(&tempRef);
			temporary.AddItem(new entry_ref(tempRef));
		} else {
			temporary.AddItem(NULL);
			failures++;
		}
	}

	for (int32 index = 0; index < fRefs.CountItems(); index++) {
		entry_ref* tempRef = temporary.ItemAt(index);
		if (tempRef == NULL)
			continue;

		BEntry entry(tempRef);
		const char* newName = fNewNames.ItemAt(index)->String();
		if (entry.Rename(newName) != B_OK) {
			// put it back
			entry.Rename(fRefs.ItemAt(index)->name);
			failures++;
			continue;
		}

		entry_ref newRef;
		entry.GetRef(&newRef);
		undoFrom.AddItem(new entry_ref(newRef));
		undoTo.AddItem(new BString(fRefs.ItemAt(index)->name));
	}

	RecordBatchRenameUndo(undoFrom, undoTo);

	if (failures > 0) {
		BAlert* alert = new BAlert("", B_TRANSLATE("Some items could not be "
				"renamed."), B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
	}

	Quit();
}


} // namespace BPrivate
