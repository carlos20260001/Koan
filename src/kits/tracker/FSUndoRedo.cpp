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


#include "Commands.h"
#include "FSUndoRedo.h"
#include "FSUtils.h"

#include <Alert.h>
#include <Autolock.h>
#include <Catalog.h>
#include <Directory.h>
#include <Locale.h>
#include <Volume.h>
#include <Node.h>
#include <Path.h>
#include <StringFormat.h>

#include <fs_attr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <map>

#include "Attributes.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FSUndoRedo"


static const int32 kUndoRedoListMaxCount = 20;


namespace BPrivate {

class UndoItem {
	public:
		virtual ~UndoItem() {}

		virtual status_t Undo() = 0;
		virtual status_t Redo() = 0;

		virtual BString Description() const = 0;
			// what was done, as in "Undo <description>"

		virtual bool ConfirmUndo() { return true; }
			// asks the user first when undoing would destroy something

		virtual void UpdateEntry(BEntry* /*entry*/, const char* /*name*/) {}
			// updates the name of the target from the source entry "entry"
};


static BString
ItemsText(const BObjectList<entry_ref, true>& list)
{
	if (list.CountItems() == 1) {
		BString text(B_TRANSLATE_COMMENT("\"%name\"",
			"A file name in an undo menu item: Undo move of \"name\""));
		text.ReplaceFirst("%name", list.ItemAt(0)->name);
		return text;
	}

	static BStringFormat format(B_TRANSLATE_COMMENT(
		"{0, plural, one{# item} other{# items}}",
		"Number of files in an undo menu item: Undo move of 3 items"));
	BString text;
	format.Format(text, list.CountItems());
	return text;
}


static BString
WithItems(const char* format, const BObjectList<entry_ref, true>& list)
{
	BString text(format);
	text.ReplaceFirst("%items", ItemsText(list));
	return text;
}


static BString
WithName(const char* format, const char* name)
{
	BString text(format);
	text.ReplaceFirst("%name", name);
	return text;
}


static bool
AskBeforeUndo(const char* text, const char* button)
{
	// Cancel is the default button: Enter must not destroy anything
	BAlert* alert = new BAlert(B_TRANSLATE("Undo"), text, button,
		B_TRANSLATE("Cancel"), NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	return alert->Go() == 0;
}


static bool
ChangedSince(const entry_ref& ref, time_t when)
{
	// a modification more than a moment after the operation; the copy loop
	// itself may touch the file while it finishes
	struct stat st;
	BEntry entry(&ref);
	return entry.GetStat(&st) == B_OK && st.st_mtime > when + 2;
}

static BObjectList<UndoItem> sUndoList, sRedoList;
static BLocker sLock("undo");

class UndoItemCopy : public UndoItem {
	public:
		UndoItemCopy(BObjectList<entry_ref, true>* sourceList, BDirectory &target,
			BList* pointList, uint32 moveMode);
		virtual ~UndoItemCopy();

		virtual status_t Undo();
		virtual status_t Redo();
		virtual BString Description() const;
		virtual bool ConfirmUndo();
		virtual void UpdateEntry(BEntry* entry, const char* name);

	private:
		BObjectList<entry_ref, true> fSourceList;
		BObjectList<entry_ref, true> fTargetList;
		entry_ref	fSourceRef, fTargetRef;
		uint32		fMoveMode;
		time_t		fTime;
};


class UndoItemMove : public UndoItem {
	public:
		/** source - list of file(s) that were moved.  Assumes ownership.
		 *	origfolder - location it was moved from
		 */
		UndoItemMove(BObjectList<entry_ref, true>* sourceList, BDirectory &target,
			BList* pointList);
		virtual ~UndoItemMove();

		virtual status_t Undo();
		virtual status_t Redo();
		virtual BString Description() const;

	private:
		BObjectList<entry_ref, true> fSourceList;
		entry_ref	fSourceRef, fTargetRef;
};


class UndoItemFolder : public UndoItem {
	public:
		UndoItemFolder(const entry_ref &ref);
			// ref - entry_ref indicating the folder created
		virtual ~UndoItemFolder();

		virtual status_t Undo();
		virtual status_t Redo();
		virtual BString Description() const;
		virtual bool ConfirmUndo();

	private:
		// this ref has two different meanings in the different states of
		// this object:
		//  - Undo() - fRef indicates the folder that was created via
		//    FSCreateNewFolderIn(...)
		//  - Redo() - fRef indicates the folder in which
		//    FSCreateNewFolderIn() should be performed
		entry_ref	fRef;
};


class UndoItemFileTemplate : public UndoItem {
	public:
		UndoItemFileTemplate(const entry_ref &fileRef, const entry_ref &templateRef);
		virtual ~UndoItemFileTemplate();

		virtual status_t Undo();
		virtual status_t Redo();
		virtual BString Description() const;
		virtual bool ConfirmUndo();

	private:
		entry_ref	fFileRef;
		entry_ref	fTemplateRef;
		time_t		fTime;
};


class UndoItemRename : public UndoItem {
	public:
		UndoItemRename(const entry_ref &origRef, const entry_ref &ref);
		UndoItemRename(const BEntry &entry, const char* newName);
		virtual ~UndoItemRename();

		virtual status_t Undo();
		virtual status_t Redo();
		virtual BString Description() const;

	private:
		entry_ref	fRef, fOrigRef;
};


class UndoItemRenameVolume : public UndoItem {
	public:
		UndoItemRenameVolume(BVolume &volume, const char* newName);
		virtual ~UndoItemRenameVolume();

		virtual status_t Undo();
		virtual status_t Redo();
		virtual BString Description() const;

	private:
		BVolume	fVolume;
		BString	fOldName, fNewName;
};


class UndoItemAttribute : public UndoItem {
	public:
		UndoItemAttribute(const entry_ref& ref, const char* attrName,
			const char* label);

		virtual status_t Undo();
		virtual status_t Redo();
		virtual BString Description() const;

	private:
		struct Value {
			bool	exists;
			uint32	type;
			BString	data;
				// raw bytes, may contain zeroes

			Value() : exists(false), type(0) {}
		};

		static void _Read(const BNode& node, const char* name, Value& value);
		static status_t _Write(BNode& node, const char* name,
			const Value& value);

		entry_ref	fRef;
		BString		fAttrName;
		BString		fLabel;
		Value		fOld;
		Value		fNew;
};


class UndoItemBatchRename : public UndoItem {
	public:
		UndoItemBatchRename(const BObjectList<entry_ref, true>& renamed,
			const BObjectList<BString, true>& oldNames);

		virtual status_t Undo();
		virtual status_t Redo();
		virtual BString Description() const;

	private:
		status_t _RenameAll(bool toOld);

		BObjectList<entry_ref, true> fNewRefs;
			// directory and new name of each item
		BObjectList<BString, true> fOldNames;
};


class UndoItemRestore : public UndoItem {
	public:
		UndoItemRestore(const BObjectList<entry_ref, true>& restored);

		virtual status_t Undo();
		virtual status_t Redo();
		virtual BString Description() const;

	private:
		BObjectList<entry_ref, true> fRestored;
		BObjectList<BString, true> fPaths;
};


//--------------------------


static status_t
ChangeListSource(BObjectList<entry_ref, true> &list, BEntry &entry)
{
	node_ref source;
	if (entry.GetNodeRef(&source) != B_OK)
		return B_ERROR;

	for (int32 index = 0; index < list.CountItems(); index++) {
		entry_ref* ref = list.ItemAt(index);

		*ref = entry_ref(source.vdevice(), source.vnode(), NULL);
	}

	return B_OK;
}


static void
AddUndoItem(UndoItem* item)
{
	BAutolock locker(sLock);

	// we have a restricted number of possible undos
	if (sUndoList.CountItems() == kUndoRedoListMaxCount)
		delete sUndoList.RemoveItemAt(sUndoList.CountItems() - 1);

	sUndoList.AddItem(item, 0);

	while (!sRedoList.IsEmpty())
		delete sRedoList.RemoveItemAt(0);
}


//	#pragma mark - Undo


Undo::~Undo()
{
	if (fUndo != NULL)
		AddUndoItem(fUndo);
}


void
Undo::UpdateEntry(BEntry* entry, const char* destName)
{
	if (fUndo != NULL)
		fUndo->UpdateEntry(entry, destName);
}


void
Undo::Remove()
{
	delete fUndo;
	fUndo = NULL;
}


MoveCopyUndo::MoveCopyUndo(BObjectList<entry_ref, true>* sourceList,
	BDirectory &dest, BList* pointList, uint32 moveMode)
{
	if (moveMode == kMoveSelectionTo)
		fUndo = new UndoItemMove(sourceList, dest, pointList);
	else
		fUndo = new UndoItemCopy(sourceList, dest, pointList, moveMode);
}


NewFileTemplateUndo::NewFileTemplateUndo(const entry_ref &fileRef, const entry_ref &templateRef)
{
	fUndo = new UndoItemFileTemplate(fileRef, templateRef);
}


NewFolderUndo::NewFolderUndo(const entry_ref &ref)
{
	fUndo = new UndoItemFolder(ref);
}


RenameUndo::RenameUndo(BEntry &entry, const char* newName)
{
	fUndo = new UndoItemRename(entry, newName);
}


RenameVolumeUndo::RenameVolumeUndo(BVolume &volume, const char* newName)
{
	fUndo = new UndoItemRenameVolume(volume, newName);
}


//	#pragma mark - UndoItemCopy


UndoItemCopy::UndoItemCopy(BObjectList<entry_ref, true>* sourceList,
	BDirectory &target, BList* /*pointList*/, uint32 moveMode)
	:
	fSourceList(*sourceList),
	fTargetList(*sourceList),
	fMoveMode(moveMode),
	fTime(time(NULL))
{
	BEntry entry(sourceList->ItemAt(0));

	BEntry sourceEntry;
	entry.GetParent(&sourceEntry);
	sourceEntry.GetRef(&fSourceRef);

	BEntry targetEntry;
	target.GetEntry(&targetEntry);
	targetEntry.GetRef(&fTargetRef);
	ChangeListSource(fTargetList, targetEntry);
}


UndoItemCopy::~UndoItemCopy()
{
}


status_t
UndoItemCopy::Undo()
{
	FSDeleteRefList(new BObjectList<entry_ref, true>(fTargetList), true, false);
	return B_OK;
}


status_t
UndoItemCopy::Redo()
{
	FSMoveToFolder(new BObjectList<entry_ref, true>(fSourceList),
		new BEntry(&fTargetRef), FSUndoMoveMode(fMoveMode), NULL);

	return B_OK;
}


void
UndoItemCopy::UpdateEntry(BEntry* entry, const char* name)
{
	entry_ref changedRef;
	if (entry->GetRef(&changedRef) != B_OK)
		return;

	for (int32 index = 0; index < fSourceList.CountItems(); index++) {
		entry_ref* ref = fSourceList.ItemAt(index);
		if (changedRef != *ref)
			continue;

		ref = fTargetList.ItemAt(index);
		ref->set_name(name);
	}
}


//	#pragma mark - UndoItemMove


UndoItemMove::UndoItemMove(BObjectList<entry_ref, true>* sourceList,
	BDirectory &target, BList* /*pointList*/)
	:
	fSourceList(*sourceList)
{
	BEntry entry(sourceList->ItemAt(0));
	BEntry source;
	entry.GetParent(&source);
	source.GetRef(&fSourceRef);

	BEntry targetEntry;
	target.GetEntry(&targetEntry);
	targetEntry.GetRef(&fTargetRef);
}


UndoItemMove::~UndoItemMove()
{
}


status_t
UndoItemMove::Undo()
{
	BObjectList<entry_ref, true>* list = new BObjectList<entry_ref, true>(fSourceList);
	BEntry entry(&fTargetRef);
	ChangeListSource(*list, entry);

	// FSMoveToFolder() owns its arguments
	FSMoveToFolder(list, new BEntry(&fSourceRef),
		FSUndoMoveMode(kMoveSelectionTo), NULL);

	return B_OK;
}


status_t
UndoItemMove::Redo()
{
	// FSMoveToFolder() owns its arguments
	FSMoveToFolder(new BObjectList<entry_ref, true>(fSourceList),
		new BEntry(&fTargetRef), FSUndoMoveMode(kMoveSelectionTo), NULL);

	return B_OK;
}


//	#pragma mark - UndoItemFileTemplate


UndoItemFileTemplate::UndoItemFileTemplate(const entry_ref &fileRef, const entry_ref &templateRef)
	:
	fFileRef(fileRef),
	fTemplateRef(templateRef),
	fTime(time(NULL))
{
}


UndoItemFileTemplate::~UndoItemFileTemplate()
{
}


status_t
UndoItemFileTemplate::Undo()
{
	FSDelete(new entry_ref(fFileRef), false, false);
	return B_OK;
}


status_t
UndoItemFileTemplate::Redo()
{
	return FSCreateNewFileTemplate(&fFileRef, &fTemplateRef);
}


//	#pragma mark -


UndoItemFolder::UndoItemFolder(const entry_ref &ref)
	:
	fRef(ref)
{
}


UndoItemFolder::~UndoItemFolder()
{
}


status_t
UndoItemFolder::Undo()
{
	FSDelete(new entry_ref(fRef), false, false);
	return B_OK;
}


status_t
UndoItemFolder::Redo()
{
	return FSCreateNewFolder(&fRef);
}


//	#pragma mark -


UndoItemRename::UndoItemRename(const entry_ref &origRef, const entry_ref &ref)
	:
	fRef(ref),
	fOrigRef(origRef)
{
}


UndoItemRename::UndoItemRename(const BEntry &entry, const char* newName)
{
	entry.GetRef(&fOrigRef);

	fRef = fOrigRef;
	fRef.set_name(newName);
}


UndoItemRename::~UndoItemRename()
{
}


status_t
UndoItemRename::Undo()
{
	BEntry entry(&fRef, false);
	return entry.Rename(fOrigRef.name);
}


status_t
UndoItemRename::Redo()
{
	BEntry entry(&fOrigRef, false);
	return entry.Rename(fRef.name);
}


//	#pragma mark - UndoItemRenameVolume


UndoItemRenameVolume::UndoItemRenameVolume(BVolume &volume,
	const char* newName)
	:
	fVolume(volume),
	fNewName(newName)
{
	char* buffer = fOldName.LockBuffer(B_FILE_NAME_LENGTH);
	if (buffer != NULL) {
		fVolume.GetName(buffer);
		fOldName.UnlockBuffer();
	}
}


UndoItemRenameVolume::~UndoItemRenameVolume()
{
}


status_t
UndoItemRenameVolume::Undo()
{
	return fVolume.SetName(fOldName.String());
}


status_t
UndoItemRenameVolume::Redo()
{
	return fVolume.SetName(fNewName.String());
}


//	#pragma mark - descriptions and confirmations


BString
UndoItemCopy::Description() const
{
	switch (fMoveMode) {
		case kDuplicateSelection:
			return WithItems(B_TRANSLATE_COMMENT("duplicate of %items",
				"Undo menu item: Undo duplicate of \"name\""), fSourceList);
		case kCreateLink:
		case kCreateRelativeLink:
			return WithItems(B_TRANSLATE_COMMENT("link to %items",
				"Undo menu item: Undo link to \"name\""), fSourceList);
		default:
			return WithItems(B_TRANSLATE_COMMENT("copy of %items",
				"Undo menu item: Undo copy of \"name\""), fSourceList);
	}
}


bool
UndoItemCopy::ConfirmUndo()
{
	if (fMoveMode == kCreateLink || fMoveMode == kCreateRelativeLink)
		return true;

	int32 changed = 0;
	for (int32 index = 0; index < fTargetList.CountItems(); index++) {
		if (ChangedSince(*fTargetList.ItemAt(index), fTime))
			changed++;
	}
	if (changed == 0)
		return true;

	return AskBeforeUndo(B_TRANSLATE("Some of the copies were changed after "
			"they were made. Undoing the copy deletes them with the changes."),
		B_TRANSLATE("Delete copies"));
}


BString
UndoItemMove::Description() const
{
	BEntry target(&fTargetRef);
	if (FSIsTrashDir(&target)) {
		return WithItems(B_TRANSLATE_COMMENT("move of %items to Trash",
			"Undo menu item: Undo move of \"name\" to Trash"), fSourceList);
	}

	return WithItems(B_TRANSLATE_COMMENT("move of %items",
		"Undo menu item: Undo move of \"name\""), fSourceList);
}


BString
UndoItemFolder::Description() const
{
	return WithName(B_TRANSLATE_COMMENT("new folder \"%name\"",
		"Undo menu item: Undo new folder \"name\""), fRef.name);
}


bool
UndoItemFolder::ConfirmUndo()
{
	BDirectory dir(&fRef);
	if (dir.InitCheck() != B_OK || dir.CountEntries() == 0)
		return true;

	return AskBeforeUndo(WithName(B_TRANSLATE("The folder \"%name\" is not "
			"empty any more. Undoing its creation deletes it with everything "
			"in it."), fRef.name).String(),
		B_TRANSLATE("Delete folder"));
}


BString
UndoItemFileTemplate::Description() const
{
	return WithName(B_TRANSLATE_COMMENT("new file \"%name\"",
		"Undo menu item: Undo new file \"name\""), fFileRef.name);
}


bool
UndoItemFileTemplate::ConfirmUndo()
{
	if (!ChangedSince(fFileRef, fTime))
		return true;

	return AskBeforeUndo(WithName(B_TRANSLATE("\"%name\" was changed after "
			"it was made. Undoing its creation deletes it with the changes."),
			fFileRef.name).String(),
		B_TRANSLATE("Delete file"));
}


BString
UndoItemRename::Description() const
{
	return WithName(B_TRANSLATE_COMMENT("rename of \"%name\"",
		"Undo menu item: Undo rename of \"old name\""), fOrigRef.name);
}


BString
UndoItemRenameVolume::Description() const
{
	return WithName(B_TRANSLATE_COMMENT("rename of \"%name\"",
		"Undo menu item: Undo rename of \"old name\""), fOldName.String());
}


//	#pragma mark - UndoItemAttribute


UndoItemAttribute::UndoItemAttribute(const entry_ref& ref, const char* attrName,
	const char* label)
	:
	fRef(ref),
	fAttrName(attrName),
	fLabel(label)
{
	BNode node(&ref);
	_Read(node, attrName, fOld);
}


void
UndoItemAttribute::_Read(const BNode& node, const char* name, Value& value)
{
	value.exists = false;
	attr_info info;
	if (node.GetAttrInfo(name, &info) != B_OK || info.size > 65536)
		return;

	char* buffer = value.data.LockBuffer(info.size + 1);
	if (buffer == NULL)
		return;

	ssize_t bytes = const_cast<BNode&>(node).ReadAttr(name, info.type, 0,
		buffer, info.size);
	value.data.UnlockBuffer(bytes > 0 ? bytes : 0);
	if (bytes < 0)
		return;

	value.exists = true;
	value.type = info.type;
}


status_t
UndoItemAttribute::_Write(BNode& node, const char* name, const Value& value)
{
	if (!value.exists)
		return node.RemoveAttr(name);

	ssize_t written = node.WriteAttr(name, value.type, 0, value.data.String(),
		value.data.Length());
	return written < 0 ? (status_t)written : B_OK;
}


status_t
UndoItemAttribute::Undo()
{
	BNode node(&fRef);
	if (node.InitCheck() != B_OK)
		return node.InitCheck();

	_Read(node, fAttrName.String(), fNew);
	return _Write(node, fAttrName.String(), fOld);
}


status_t
UndoItemAttribute::Redo()
{
	BNode node(&fRef);
	if (node.InitCheck() != B_OK)
		return node.InitCheck();

	return _Write(node, fAttrName.String(), fNew);
}


BString
UndoItemAttribute::Description() const
{
	BString text(B_TRANSLATE_COMMENT("change of %attribute of \"%name\"",
		"Undo menu item: Undo change of Rating of \"song.mp3\""));
	text.ReplaceFirst("%attribute", fLabel);
	text.ReplaceFirst("%name", fRef.name);
	return text;
}


//	#pragma mark - UndoItemRestore


UndoItemRestore::UndoItemRestore(const BObjectList<entry_ref, true>& restored)
	:
	fRestored(restored)
{
	for (int32 index = 0; index < restored.CountItems(); index++) {
		BPath path(restored.ItemAt(index));
		fPaths.AddItem(new BString(path.Path()));
	}
}


status_t
UndoItemRestore::Undo()
{
	// back into the Trash of their volumes, without a new undo item
	std::map<dev_t, BObjectList<entry_ref, true>*> lists;
	for (int32 index = 0; index < fRestored.CountItems(); index++) {
		entry_ref* ref = fRestored.ItemAt(index);
		BObjectList<entry_ref, true>*& list = lists[ref->device];
		if (list == NULL)
			list = new BObjectList<entry_ref, true>(10);
		list->AddItem(new entry_ref(*ref));
	}

	std::map<dev_t, BObjectList<entry_ref, true>*>::iterator iterator;
	for (iterator = lists.begin(); iterator != lists.end(); iterator++) {
		FSMoveToFolder(iterator->second, NULL,
			FSUndoMoveMode(kMoveSelectionTo), NULL);
	}

	return B_OK;
}


status_t
UndoItemRestore::Redo()
{
	// find the items in the Trash by where they came from
	BObjectList<entry_ref, true>* list = new BObjectList<entry_ref, true>(10);
	std::map<dev_t, bool> searched;
	for (int32 index = 0; index < fRestored.CountItems(); index++) {
		dev_t device = fRestored.ItemAt(index)->device;
		if (searched[device])
			continue;
		searched[device] = true;

		BDirectory trash;
		if (FSGetTrashDir(&trash, device) != B_OK)
			continue;

		BEntry entry;
		while (trash.GetNextEntry(&entry) == B_OK) {
			BNode node(&entry);
			BString originalPath;
			if (node.ReadAttrString(kAttrOriginalPath, &originalPath) != B_OK)
				continue;

			for (int32 i = 0; i < fPaths.CountItems(); i++) {
				if (*fPaths.ItemAt(i) == originalPath) {
					entry_ref ref;
					if (entry.GetRef(&ref) == B_OK)
						list->AddItem(new entry_ref(ref));
					break;
				}
			}
		}
	}

	FSRestoreRefList(list, true, false);
	return B_OK;
}


BString
UndoItemRestore::Description() const
{
	return WithItems(B_TRANSLATE_COMMENT("restore of %items",
		"Undo menu item: Undo restore of \"name\" from the Trash"),
		fRestored);
}


//	#pragma mark - UndoItemBatchRename


UndoItemBatchRename::UndoItemBatchRename(
	const BObjectList<entry_ref, true>& renamed,
	const BObjectList<BString, true>& oldNames)
	:
	fNewRefs(renamed),
	fOldNames(oldNames)
{
}


status_t
UndoItemBatchRename::_RenameAll(bool toOld)
{
	// through temporary names, as names may move around within the set
	int32 count = fNewRefs.CountItems();
	BObjectList<entry_ref, true> temporary(count);
	for (int32 index = 0; index < count; index++) {
		entry_ref from = *fNewRefs.ItemAt(index);
		if (toOld == false)
			from.set_name(fOldNames.ItemAt(index)->String());

		char tempName[B_FILE_NAME_LENGTH];
		snprintf(tempName, sizeof(tempName), ".tracker-undo-%" B_PRId32,
			index);
		BEntry entry(&from);
		entry_ref tempRef;
		if (entry.Rename(tempName) == B_OK && entry.GetRef(&tempRef) == B_OK)
			temporary.AddItem(new entry_ref(tempRef));
		else
			temporary.AddItem(NULL);
	}

	for (int32 index = 0; index < count; index++) {
		if (temporary.ItemAt(index) == NULL)
			continue;
		BEntry entry(temporary.ItemAt(index));
		entry.Rename(toOld ? fOldNames.ItemAt(index)->String()
			: fNewRefs.ItemAt(index)->name);
	}

	return B_OK;
}


status_t
UndoItemBatchRename::Undo()
{
	return _RenameAll(true);
}


status_t
UndoItemBatchRename::Redo()
{
	return _RenameAll(false);
}


BString
UndoItemBatchRename::Description() const
{
	BString text(B_TRANSLATE_COMMENT("rename of %items",
		"Undo menu item: Undo rename of 3 items"));
	BObjectList<entry_ref, true> refs(fNewRefs);
	text.ReplaceFirst("%items", ItemsText(refs));
	return text;
}


void
RecordBatchRenameUndo(const BObjectList<entry_ref, true>& renamed,
	const BObjectList<BString, true>& oldNames)
{
	if (!renamed.IsEmpty())
		AddUndoItem(new UndoItemBatchRename(renamed, oldNames));
}


AttributeUndo::AttributeUndo(const entry_ref& ref, const char* attrName,
	const char* label)
{
	fUndo = new UndoItemAttribute(ref, attrName, label);
}


void
RecordRestoreUndo(const BObjectList<entry_ref, true>& restored)
{
	if (!restored.IsEmpty())
		AddUndoItem(new UndoItemRestore(restored));
}


//	#pragma mark - FSUndo() and FSRedo() functions


bool
FSGetUndoDescription(BString& description)
{
	BAutolock locker(sLock);

	UndoItem* item = sUndoList.FirstItem();
	if (item == NULL)
		return false;

	description = item->Description();
	return true;
}


bool
FSGetRedoDescription(BString& description)
{
	BAutolock locker(sLock);

	UndoItem* item = sRedoList.FirstItem();
	if (item == NULL)
		return false;

	description = item->Description();
	return true;
}


void
FSUndo()
{
	UndoItem* undoItem;
	{
		BAutolock locker(sLock);
		undoItem = sUndoList.RemoveItemAt(0);
		if (undoItem == NULL)
			return;
	}

	// ask (if needed) without holding the lock: a copy that finishes
	// meanwhile adds its own undo item
	if (!undoItem->ConfirmUndo()) {
		BAutolock locker(sLock);
		sUndoList.AddItem(undoItem, 0);
		return;
	}

	undoItem->Undo();
		// ToDo: evaluate return code

	BAutolock locker(sLock);
	if (sRedoList.CountItems() == kUndoRedoListMaxCount)
		delete sRedoList.RemoveItemAt(sRedoList.CountItems() - 1);

	sRedoList.AddItem(undoItem, 0);
}


void
FSRedo()
{
	BAutolock locker(sLock);

	UndoItem* undoItem = sRedoList.RemoveItemAt(0);
	if (undoItem == NULL)
		return;

	undoItem->Redo();
		// ToDo: evaluate return code

	if (sUndoList.CountItems() == kUndoRedoListMaxCount)
		delete sUndoList.RemoveItemAt(sUndoList.CountItems() - 1);

	sUndoList.AddItem(undoItem, 0);
}

}	// namespace BPrivate
