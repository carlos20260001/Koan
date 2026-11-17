/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "FolderSizes.h"

#include <Autolock.h>
#include <Directory.h>
#include <Locker.h>
#include <Message.h>
#include <OS.h>

#include <fs_info.h>
#include <string.h>
#include <sys/stat.h>

#include <deque>
#include <map>
#include <vector>


namespace BPrivate {


struct NodeKey {
	dev_t	device;
	ino_t	node;

	NodeKey(const node_ref& ref) : device(ref.device()), node(ref.node()) {}

	bool operator<(const NodeKey& other) const
	{
		if (device != other.device)
			return device < other.device;
		return node < other.node;
	}
};


struct CachedSize {
	off_t		size;
	bigtime_t	when;
};


struct Request {
	entry_ref	ref;
	node_ref	node;
	BMessenger	target;
	bigtime_t	maxAge;
};


static const int32 kMaxEntriesPerFolder = 2000000;
static const size_t kMaxCachedFolders = 20000;

static BLocker sLock("folder sizes");
static std::map<NodeKey, CachedSize>* sCache;
static std::deque<Request>* sQueue;
static sem_id sQueueSem = -1;
static thread_id sThread = -1;


static void
SendSize(const Request& request, off_t size)
{
	BMessage message(kFolderSizeReady);
	message.AddInt32("device", request.node.device());
	message.AddInt64("node", request.node.node());
	message.AddInt64("size", size);
	request.target.SendMessage(&message, (BHandler*)NULL, 0);
}


static off_t
WalkFolder(const entry_ref& ref, dev_t device)
{
	// iterative, so deep trees cannot exhaust the stack; links are not
	// followed and other volumes mounted below are skipped
	off_t total = 0;
	int32 entries = 0;

	std::vector<BDirectory*> stack;
	BDirectory* root = new BDirectory(&ref);
	if (root->InitCheck() != B_OK) {
		delete root;
		return 0;
	}
	stack.push_back(root);

	while (!stack.empty()) {
		BDirectory* dir = stack.back();
		stack.pop_back();

		BEntry entry;
		while (dir->GetNextEntry(&entry, false) == B_OK) {
			if (++entries > kMaxEntriesPerFolder)
				break;

			struct stat st;
			if (entry.GetStat(&st) != B_OK || st.st_dev != device)
				continue;

			if (S_ISREG(st.st_mode))
				total += st.st_size;
			else if (S_ISDIR(st.st_mode)) {
				BDirectory* subDir = new BDirectory(&entry);
				if (subDir->InitCheck() == B_OK)
					stack.push_back(subDir);
				else
					delete subDir;
			}
		}
		delete dir;

		if (entries > kMaxEntriesPerFolder)
			break;
	}

	for (size_t i = 0; i < stack.size(); i++)
		delete stack[i];

	return total;
}


static status_t
CalculatorThread(void*)
{
	while (true) {
		if (acquire_sem_etc(sQueueSem, 1, B_RELATIVE_TIMEOUT, 60000000)
				!= B_OK) {
			// idle for a minute: let the thread go, a request starts a new one
			BAutolock locker(sLock);
			if (sQueue->empty()) {
				sThread = -1;
				return B_OK;
			}
			continue;
		}

		Request request;
		{
			BAutolock locker(sLock);
			if (sQueue->empty())
				continue;
			request = sQueue->front();
			sQueue->pop_front();

			// asked for again meanwhile, or by another window
			std::map<NodeKey, CachedSize>::iterator found
				= sCache->find(NodeKey(request.node));
			if (found != sCache->end() && (request.maxAge < 0
					|| system_time() - found->second.when <= request.maxAge)) {
				off_t size = found->second.size;
				locker.Unlock();
				SendSize(request, size);
				continue;
			}
		}

		if (!request.target.IsValid())
			continue;

		off_t size = WalkFolder(request.ref, request.node.device());

		{
			BAutolock locker(sLock);
			if (sCache->size() >= kMaxCachedFolders)
				sCache->clear();
			CachedSize cached = { size, system_time() };
			(*sCache)[NodeKey(request.node)] = cached;
		}

		SendSize(request, size);
	}

	return B_OK;
}


static void
InitOnce()
{
	if (sCache != NULL)
		return;

	sCache = new std::map<NodeKey, CachedSize>;
	sQueue = new std::deque<Request>;
	sQueueSem = create_sem(0, "folder size requests");
}


bool
GetFolderSize(const node_ref& node, off_t* size)
{
	BAutolock locker(sLock);
	InitOnce();

	std::map<NodeKey, CachedSize>::iterator found = sCache->find(NodeKey(node));
	if (found == sCache->end())
		return false;

	*size = found->second.size;
	return true;
}


void
RequestFolderSize(const entry_ref& ref, const node_ref& node,
	const BMessenger& target, bigtime_t maxAge)
{
	BAutolock locker(sLock);
	InitOnce();

	Request request;
	request.ref = ref;
	request.node = node;
	request.target = target;
	request.maxAge = maxAge;

	std::map<NodeKey, CachedSize>::iterator found = sCache->find(NodeKey(node));
	if (found != sCache->end()
		&& (maxAge < 0 || system_time() - found->second.when <= maxAge)) {
		off_t size = found->second.size;
		locker.Unlock();
		SendSize(request, size);
		return;
	}

	sQueue->push_back(request);

	if (sThread < 0) {
		sThread = spawn_thread(CalculatorThread, "folder sizes",
			B_LOW_PRIORITY, NULL);
		if (sThread >= 0)
			resume_thread(sThread);
	}
	release_sem(sQueueSem);
}


void
ForgetFolderSize(const node_ref& node)
{
	BAutolock locker(sLock);
	InitOnce();
	sCache->erase(NodeKey(node));
}


bool
CanCalculateFolderSizes(dev_t device)
{
	fs_info info;
	if (fs_stat_dev(device, &info) != 0)
		return false;

	static const char* kNetworkFileSystems[] = {
		"smbfs", "nfs", "nfs4", "netfs", "userlandfs", "webdavfs", NULL
	};
	for (int32 i = 0; kNetworkFileSystems[i] != NULL; i++) {
		if (strcmp(info.fsh_name, kNetworkFileSystems[i]) == 0)
			return false;
	}

	return true;
}


} // namespace BPrivate
