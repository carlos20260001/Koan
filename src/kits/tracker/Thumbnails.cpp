/*
 * Copyright 2021-2022, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Augustin Cavalier <waddlesplash>
 *		John Scipione <jscipione@gmail.com>
 */
#include "Thumbnails.h"

#include <list>
#include <set>
#include <fs_attr.h>

#include <Application.h>
#include <Autolock.h>
#include <BitmapStream.h>
#include <Messenger.h>
#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Roster.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <TranslationUtils.h>
#include <TypeConstants.h>
#include <View.h>
#include <Volume.h>

#include <AutoDeleter.h>
#include <JobQueue.h>
#include <QuickLookDefs.h>

#include "Attributes.h"
#include "Commands.h"
#include "FolderSizes.h"
#include "FSUtils.h"
#include "TrackerSettings.h"


#ifdef B_XXL_ICON
#	undef B_XXL_ICON
#endif
#define B_XXL_ICON 128


namespace BPrivate {


//	#pragma mark - thumbnail generation


enum ThumbnailWorkers {
	SMALLER_FILES_WORKER = 0,
	LARGER_FILES_WORKER,

	TOTAL_THUMBNAIL_WORKERS
};
using BSupportKit::BPrivate::JobQueue;
static JobQueue* sThumbnailWorkers[TOTAL_THUMBNAIL_WORKERS];

static std::list<GenerateThumbnailJob*> sActiveJobs;
static BLocker sActiveJobsLock;


static BRect
ThumbBounds(BBitmap* icon, float aspectRatio)
{
	BRect thumbBounds;

	if ((icon->Bounds().Width() / icon->Bounds().Height()) == aspectRatio)
		return icon->Bounds();

	if (aspectRatio > 1) {
		// wide
		thumbBounds = BRect(0, 0, icon->Bounds().IntegerWidth() - 1,
			floorf((icon->Bounds().IntegerHeight() - 1) / aspectRatio));
		thumbBounds.OffsetBySelf(0, floorf((icon->Bounds().IntegerHeight()
			- thumbBounds.IntegerHeight()) / 2.0f));
	} else if (aspectRatio < 1) {
		// tall
		thumbBounds = BRect(0, 0, floorf((icon->Bounds().IntegerWidth() - 1)
			* aspectRatio), icon->Bounds().IntegerHeight() - 1);
		thumbBounds.OffsetBySelf(floorf((icon->Bounds().IntegerWidth()
			- thumbBounds.IntegerWidth()) / 2.0f), 0);
	} else {
		// square
		thumbBounds = icon->Bounds();
	}

	return thumbBounds;
}


static status_t
ScaleBitmap(BBitmap* source, BBitmap& dest, BRect bounds, color_space colorSpace)
{
	dest = BBitmap(bounds, colorSpace, true);
	BView view(dest.Bounds(), "", B_FOLLOW_NONE, B_WILL_DRAW);
	dest.AddChild(&view);
	if (view.LockLooper()) {
		// fill with transparent
		view.SetLowColor(B_TRANSPARENT_COLOR);
		view.FillRect(view.Bounds(), B_SOLID_LOW);
		// draw bitmap
		view.SetDrawingMode(B_OP_ALPHA);
		view.SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		view.DrawBitmap(source, source->Bounds(),
			ThumbBounds(&dest, source->Bounds().Width()
				/ source->Bounds().Height()),
			B_FILTER_BITMAP_BILINEAR);
		view.Sync();
		view.UnlockLooper();
	}
	dest.RemoveChild(&view);
	return B_OK;
}


static status_t
ScaleBitmap(BBitmap* source, BBitmap& dest, BSize size, color_space colorSpace)
{
	return ScaleBitmap(source, dest, BRect(BPoint(0, 0), size), colorSpace);
}


class GenerateThumbnailJob : public BSupportKit::BJob {
public:
	GenerateThumbnailJob(Model* model, const BFile& file,
			BSize requestedSize, color_space colorSpace)
		: BJob("GenerateThumbnail"),
		  fMimeType(model->MimeType()),
		  fRef(*model->EntryRef()),
		  fRequestedSize(requestedSize),
		  fColorSpace(colorSpace)
	{
		fFile = new(std::nothrow) BFile(file);
		fFile->GetNodeRef((node_ref*)&fNodeRef);

		BAutolock lock(sActiveJobsLock);
		sActiveJobs.push_back(this);
	}
	virtual ~GenerateThumbnailJob()
	{
		delete fFile;

		BAutolock lock(sActiveJobsLock);
		sActiveJobs.remove(this);
	}

	status_t InitCheck()
	{
		if (fFile == NULL)
			return B_NO_MEMORY;
		return BJob::InitCheck();
	}

	virtual status_t Execute();

public:
	const BString fMimeType;
	const entry_ref fRef;
	const node_ref fNodeRef;
	const BSize fRequestedSize;
	const color_space fColorSpace;

private:
	BFile* fFile;
};


// Types nothing makes a thumbnail of, as the quick_look server said; not
// asked again until Tracker restarts.
static std::set<BString> sNoThumbnailTypes;
static BLocker sNoThumbnailTypesLock("no thumbnail types");

// Files whose thumbnail could not be made: until they change, their type's
// icon is shown instead of asking again at every redraw.
static std::set<std::pair<dev_t, ino_t> > sFailedNodes;


static bool
ThumbnailFailedBefore(const node_ref& node)
{
	BAutolock locker(sNoThumbnailTypesLock);
	return sFailedNodes.find(std::make_pair(node.device(), node.node()))
		!= sFailedNodes.end();
}


static bool
IsImageType(const char* type)
{
	return type != NULL && BString(type).IStartsWith("image");
}


static BBitmap*
ThumbnailFromQuickLook(const entry_ref& ref, const char* mimeType, int32 size)
{
	// videos, documents and the like: the Quick Look add-ons draw them, in
	// the quick_look server, so their code never runs in Tracker
	BMessenger server(B_QUICK_LOOK_SIGNATURE);
	if (!server.IsValid()) {
		// started on demand; give it a moment to be there
		be_roster->Launch(B_QUICK_LOOK_SIGNATURE);
		for (int32 i = 0; i < 20 && !server.IsValid(); i++) {
			snooze(100000);
			server = BMessenger(B_QUICK_LOOK_SIGNATURE);
		}
		if (!server.IsValid())
			return NULL;
	}

	BMessage request(kMsgQuickLookThumbnail);
	request.AddRef("ref", &ref);
	request.AddInt32("size", size);
	BMessage reply;
	if (server.SendMessage(&request, &reply, 5000000, 60000000) != B_OK)
		return NULL;

	BMessage archive;
	if (reply.FindMessage("bitmap", &archive) == B_OK) {
		BBitmap* bitmap = new BBitmap(&archive);
		if (bitmap->InitCheck() == B_OK)
			return bitmap;
		delete bitmap;
		return NULL;
	}

	if (reply.GetInt32("error", B_ERROR) == B_NOT_SUPPORTED) {
		BAutolock locker(sNoThumbnailTypesLock);
		sNoThumbnailTypes.insert(BString(mimeType));
	}
	return NULL;
}


status_t
GenerateThumbnailJob::Execute()
{
	BBitmap* image = NULL;
	status_t status;
	bool isImage = IsImageType(fMimeType);
	if (isImage) {
		BBitmapStream imageStream;
		status = BTranslatorRoster::Default()->Translate(fFile, NULL, NULL,
			&imageStream, B_TRANSLATOR_BITMAP, 0, fMimeType);
		if (status != B_OK)
			return status;

		status = imageStream.DetachBitmap(&image);
		if (status != B_OK)
			return status;
	} else {
		image = ThumbnailFromQuickLook(fRef, fMimeType, B_XXL_ICON);
		if (image == NULL) {
			{
				BAutolock locker(sNoThumbnailTypesLock);
				sFailedNodes.insert(std::make_pair(fNodeRef.device(),
					fNodeRef.node()));
			}
			// draw the type's icon instead
			BMessage message(kUpdateThumbnail);
			if (message.AddNodeRef("noderef", &fNodeRef) == B_OK)
				be_app->PostMessage(&message);
			return B_NOT_SUPPORTED;
		}
	}

	// we have translated the image file into a BBitmap

	// now, scale and directly insert into the icon cache
	BBitmap tmp(NULL, false);
	ScaleBitmap(image, tmp, fRequestedSize, fColorSpace);

	BBitmap* cacheThumb = new BBitmap(tmp.Bounds(), 0, tmp.ColorSpace());
	cacheThumb->ImportBits(&tmp);

	NodeIconCache* nodeIconCache = &IconCache::sIconCache->fNodeCache;
	AutoLocker<NodeIconCache> cacheLocker(nodeIconCache);
	NodeCacheEntry* entry = nodeIconCache->FindItem(&fNodeRef);
	if (entry == NULL)
		entry = nodeIconCache->AddItem(&fNodeRef);
	if (entry == NULL) {
		delete cacheThumb;
		return B_NO_MEMORY;
	}

	entry->SetIcon(cacheThumb, kNormalIcon, fRequestedSize);
	cacheLocker.Unlock();

	// write values to attributes
	bool thumbnailWritten = false;
	bool canWrite = true;
	if (isImage) {
		// the picture's size, for a column; not for a page or a frame
		const int32 width = image->Bounds().IntegerWidth() + 1;
		const size_t written = fFile->WriteAttr("Media:Width", B_INT32_TYPE,
			0, &width, sizeof(int32));
		canWrite = written == sizeof(int32);
		if (canWrite) {
			const int32 height = image->Bounds().IntegerHeight() + 1;
			fFile->WriteAttr("Media:Height", B_INT32_TYPE, 0, &height,
				sizeof(int32));
		}
	}
	if (canWrite) {

		// convert image into a 128x128 WebP image and stash it
		BBitmap thumb(NULL, false);
		ScaleBitmap(image, thumb, B_XXL_ICON, fColorSpace);

		BBitmap* thumbPointer = &thumb;
		BBitmapStream thumbStream(thumbPointer);
		BMallocIO stream;
		if (BTranslatorRoster::Default()->Translate(&thumbStream,
					NULL, NULL, &stream, B_WEBP_FORMAT) == B_OK
				&& thumbStream.DetachBitmap(&thumbPointer) == B_OK) {
			// write WebP image data into an attribute
			status = fFile->WriteAttr(kAttrThumbnail, B_RAW_TYPE, 0,
				stream.Buffer(), stream.BufferLength());
			thumbnailWritten = (status == B_OK);

			// write thumbnail creation time into an attribute
			int64_t created = real_time_clock();
			fFile->WriteAttr(kAttrThumbnailCreationTime, B_TIME_TYPE,
				0, &created, sizeof(int64_t));
		}
	}

	delete image;

	// Manually trigger an icon refresh, if necessary.
	// (If the attribute was written, node monitoring will handle this automatically.)
	if (!thumbnailWritten) {
		// send Tracker a message to tell it to update the thumbnail
		BMessage message(kUpdateThumbnail);
		if (message.AddNodeRef("noderef", &fNodeRef) == B_OK)
			be_app->PostMessage(&message);
	}

	return B_OK;
}


static status_t
thumbnail_worker(void* castToJobQueue)
{
	JobQueue* queue = (JobQueue*)castToJobQueue;
	while (true) {
		BSupportKit::BJob* job;
		status_t status = queue->Pop(B_INFINITE_TIMEOUT, false, &job);
		if (status == B_INTERRUPTED)
			continue;
		if (status != B_OK)
			break;

		job->Run();
		delete job;
	}

	return B_OK;
}


static status_t
GenerateThumbnail(Model* model, color_space colorSpace, BSize size)
{
	// First check we do not have a job queued already.
	BAutolock jobsLock(sActiveJobsLock);
	for (std::list<GenerateThumbnailJob*>::iterator it = sActiveJobs.begin();
			it != sActiveJobs.end(); it++) {
		if ((*it)->fNodeRef == *model->NodeRef())
			return B_BUSY;
	}
	jobsLock.Unlock();

	BFile* file = dynamic_cast<BFile*>(model->Node());
	if (file == NULL)
		return B_NOT_SUPPORTED;

	struct stat st;
	status_t status = file->GetStat(&st);
	if (status != B_OK)
		return status;

	GenerateThumbnailJob* job = new(std::nothrow) GenerateThumbnailJob(model,
		*file, size, colorSpace);
	ObjectDeleter<GenerateThumbnailJob> jobDeleter(job);
	if (job == NULL)
		return B_NO_MEMORY;
	if (job->InitCheck() != B_OK)
		return job->InitCheck();

	JobQueue** jobQueue;
	if (st.st_size >= (128 * kKBSize)) {
		jobQueue = &sThumbnailWorkers[LARGER_FILES_WORKER];
	} else {
		jobQueue = &sThumbnailWorkers[SMALLER_FILES_WORKER];
	}

	if ((*jobQueue) == NULL) {
		// We need to create the worker.
		*jobQueue = new(std::nothrow) JobQueue();
		if ((*jobQueue) == NULL)
			return B_NO_MEMORY;
		if ((*jobQueue)->InitCheck() != B_OK)
			return (*jobQueue)->InitCheck();
		thread_id thread = spawn_thread(thumbnail_worker, "thumbnail worker",
			B_NORMAL_PRIORITY, *jobQueue);
		if (thread < B_OK)
			return thread;
		resume_thread(thread);
	}

	jobDeleter.Detach();
	status = (*jobQueue)->AddJob(job);
	if (status == B_OK)
		return B_BUSY;

	return status;
}


//	#pragma mark - thumbnail fetching


status_t
GetThumbnailFromAttr(Model* model, BBitmap* icon, BSize size)
{
	if (model == NULL || icon == NULL)
		return B_BAD_VALUE;

	status_t result = model->InitCheck();
	if (result != B_OK)
		return result;

	result = icon->InitCheck();
	if (result != B_OK)
		return result;

	BNode* node = model->Node();
	if (node == NULL)
		return B_BAD_VALUE;

	// a page or a frame says nothing at mini icon size: the type's icon does
	if (!IsImageType(model->MimeType()) && size.IntegerWidth() + 1 < 32)
		return B_NOT_SUPPORTED;


	// look for a thumbnail in an attribute
	time_t modtime;
	int64_t thumbnailCreated;
	if (node->GetModificationTime(&modtime) == B_OK
		&& node->ReadAttr(kAttrThumbnailCreationTime, B_TIME_TYPE, 0,
			&thumbnailCreated, sizeof(int64_t)) == sizeof(int64_t)) {
		if (thumbnailCreated > modtime) {
			// file has not changed, try to return an existing thumbnail
			attr_info attrInfo;
			if (node->GetAttrInfo(kAttrThumbnail, &attrInfo) == B_OK) {
				BMallocIO webpData;
				webpData.SetSize(attrInfo.size);
				if (node->ReadAttr(kAttrThumbnail, attrInfo.type, 0,
						(void*)webpData.Buffer(), attrInfo.size) == attrInfo.size) {
					BBitmap thumb(BTranslationUtils::GetBitmap(&webpData));

					// convert thumb to icon size
					if ((size.IntegerWidth() + 1) == B_XXL_ICON) {
						// import icon data from attribute without resizing
						result = icon->ImportBits(&thumb);
					} else {
						// down-scale thumb to icon size
						// TODO don't make a copy, allow icon to accept views?
						BBitmap tmp(NULL, false);
						ScaleBitmap(&thumb, tmp, icon->Bounds(), icon->ColorSpace());

						// copy tmp bitmap into icon
						result = icon->ImportBits(&tmp);
					}
					// we found a thumbnail
					if (result == B_OK)
						return result;
				}
			}
			// else we did not find a thumbnail
		} else {
			// file changed, remove all thumb attrs
			char attrName[B_ATTR_NAME_LENGTH];
			while (node->GetNextAttrName(attrName) == B_OK) {
				if (BString(attrName).StartsWith(kAttrThumbnail))
					node->RemoveAttr(attrName);
			}
		}
	}

	if (ShouldGenerateThumbnail(model->MimeType())) {
		// not over the network: reading a whole video to draw an icon is
		// slow there
		if (!IsImageType(model->MimeType())
			&& (!CanCalculateFolderSizes(model->NodeRef()->device())
				|| ThumbnailFailedBefore(*model->NodeRef()))) {
			return B_NOT_SUPPORTED;
		}
		return GenerateThumbnail(model, icon->ColorSpace(), size);
	}

	return B_NOT_SUPPORTED;
}


bool
ShouldGenerateThumbnail(const char* type)
{
	// images through the translators, everything else through the Quick
	// Look add-ons unless they said they cannot
	if (!TrackerSettings().GenerateImageThumbnails() || type == NULL)
		return false;
	if (IsImageType(type))
		return true;

	BString string(type);
	if (string.Length() == 0 || string == B_FILE_MIME_TYPE
		|| string.IStartsWith("application/x-vnd.be")
		|| string.IStartsWith("application/x-vnd.vos")
		|| string == "application/x-vnd.Be-elfexecutable") {
		// executables, folders, queries, people files: their icons say more
		return false;
	}

	BAutolock locker(sNoThumbnailTypesLock);
	return sNoThumbnailTypes.find(string) == sNoThumbnailTypes.end();
}


} // namespace BPrivate
