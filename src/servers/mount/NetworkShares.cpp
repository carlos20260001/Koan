/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "NetworkShares.h"

#include <ctype.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <Application.h>
#include <AutoLocker.h>
#include <Catalog.h>
#include <Directory.h>
#include <DriverSettingsMessageAdapter.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <fs_info.h>
#include <fs_volume.h>
#include <Key.h>
#include <KeyStore.h>
#include <Notification.h>
#include <Path.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "MountServer.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NetworkShares"


static const char* kSettingsFile = "network_shares";

// The network is not there when the system starts, and wireless takes its
// time. This is how long a share is tried again for once there is reason to
// think it could be had: the system has started, or the network has changed.
static const bigtime_t kRetryPeriod = 15 * 60 * 1000000LL;
static const bigtime_t kFirstRetryDelay = 3 * 1000000LL;
static const bigtime_t kMaxRetryDelay = 60 * 1000000LL;

static const uint32 kJobRetry = 'retr';
static const uint32 kJobCheckPasswords = 'ckpw';


const static settings_template kShareTemplate[] = {
	{B_INT32_TYPE, "id", NULL},
	{B_STRING_TYPE, "name", NULL},
	{B_STRING_TYPE, "server", NULL},
	{B_STRING_TYPE, "share", NULL},
	{B_STRING_TYPE, "path", NULL},
	{B_STRING_TYPE, "user", NULL},
	{B_STRING_TYPE, "domain", NULL},
	{B_BOOL_TYPE, "mount_at_startup", NULL},
	{B_BOOL_TYPE, "read_only", NULL},
	{B_BOOL_TYPE, "encrypt", NULL},
	{0, NULL, NULL}
};

const static settings_template kSharesTemplate[] = {
	{B_MESSAGE_TYPE, "share", kShareTemplate},
	{0, NULL, NULL}
};


struct NetworkShares::Share {
	int32		id;
	BString		name;
	BString		server;
	BString		share;
	BString		path;
	BString		user;
	BString		domain;
	bool		mountAtStartup;
	bool		readOnly;
	bool		encrypt;

	// what is not in the settings
	bool		hasPassword;
	bool		mounted;
		// as far as the worker knows, which is what it goes by
	bool		wanted;
		// it is to be mounted, and tried again if it cannot be
	status_t	error;
	bigtime_t	nextTry;
	bigtime_t	retryDelay;

	Share()
		:
		id(-1),
		mountAtStartup(true),
		readOnly(false),
		encrypt(false),
		hasPassword(false),
		mounted(false),
		wanted(false),
		error(B_OK),
		nextTry(0),
		retryDelay(kFirstRetryDelay)
	{
	}
};


struct NetworkShares::Job {
	uint32		what;
	int32		id;
	BMessage*	request;
};


//	#pragma mark - Linux SMB mounts


/*!	The host part of a share's "server", which may end in ":port". An IPv6
	address is written in brackets then, as in "[::1]:4445".
*/
static BString
server_host(const BString& server)
{
	BString host(server);
	if (host.StartsWith("[")) {
		int32 end = host.FindFirst(']');
		if (end > 0)
			host.Truncate(end).Remove(0, 1);
		return host;
	}
	int32 colon = host.FindFirst(':');
	if (colon >= 0 && host.FindLast(':') == colon)
		host.Truncate(colon);
	return host;
}


static int32
server_port(const BString& server)
{
	int32 colon = server.FindLast(':');
	if (colon < 0 || (!server.StartsWith("[") && server.FindFirst(':') != colon))
		return 0;
	if (server.StartsWith("[") && server.FindLast(']') > colon)
		return 0;
	return atoi(server.String() + colon + 1);
}


/*!	The uid and gid the files of a share belong to: the session's user,
	which the mount server, running as root, learns the way AutoMounter
	does (janus starts it with sudo's SUDO_UID/SUDO_GID).
*/
static void
session_ids(BString& options)
{
	const char* uid = getenv("SUDO_UID");
	const char* gid = getenv("SUDO_GID");
	if (uid != NULL && isdigit((unsigned char)*uid))
		options << ",uid=" << uid;
	if (gid != NULL && isdigit((unsigned char)*gid))
		options << ",gid=" << gid;
}


/*!	Mounts //server/share[/path] at \a mountPoint with mount.cifs from
	cifs-utils. It resolves the server's name, which the kernel's SMB client
	cannot do by itself, and takes the password from $PASSWD, so it is on no
	command line. Without cifs-utils this is \c B_DEVICE_NOT_FOUND, which
	is not tried again, like air/OS's smbfs not being installed.
*/
static status_t
mount_cifs(const BString& server, const BString& share, const BString& path,
	const BString& user, const BString& password, const BString& domain,
	bool readOnly, bool encrypt, const BString& mountPoint)
{
	static const char* const kMountCifs[] = {
		"/usr/sbin/mount.cifs", "/sbin/mount.cifs", NULL
	};
	const char* mountCifs = NULL;
	for (int32 i = 0; kMountCifs[i] != NULL; i++) {
		if (access(kMountCifs[i], X_OK) == 0) {
			mountCifs = kMountCifs[i];
			break;
		}
	}
	if (mountCifs == NULL)
		return B_DEVICE_NOT_FOUND;

	// mount.cifs splits its options at commas and has no quoting
	if (user.FindFirst(',') >= 0 || domain.FindFirst(',') >= 0
		|| user.FindFirst('=') >= 0 || domain.FindFirst('=') >= 0) {
		return B_BAD_VALUE;
	}

	BString unc;
	unc.SetToFormat("//%s/%s", server_host(server).String(), share.String());
	if (!path.IsEmpty()) {
		BString subPath(path);
		subPath.ReplaceAll('\\', '/');
		while (subPath.StartsWith("/"))
			subPath.Remove(0, 1);
		if (!subPath.IsEmpty())
			unc << "/" << subPath;
	}

	BString options(readOnly ? "ro" : "rw");
	if (user.IsEmpty())
		options << ",guest";
	else
		options << ",username=" << user;
	if (!domain.IsEmpty())
		options << ",domain=" << domain;
	if (int32 port = server_port(server))
		options << ",port=" << port;
	if (encrypt)
		options << ",seal";
	options << ",nosuid,nodev";
	session_ids(options);

	int errorPipe[2];
	if (pipe2(errorPipe, O_CLOEXEC) != 0)
		return -errno;

	pid_t child = fork();
	if (child < 0) {
		status_t error = -errno;
		close(errorPipe[0]);
		close(errorPipe[1]);
		return error;
	}

	if (child == 0) {
		dup2(errorPipe[1], STDERR_FILENO);
		dup2(errorPipe[1], STDOUT_FILENO);
		if (!user.IsEmpty() && !password.IsEmpty())
			setenv("PASSWD", password.String(), 1);
		else
			unsetenv("PASSWD");
		execl(mountCifs, "mount.cifs", unc.String(), mountPoint.String(),
			"-o", options.String(), (char*)NULL);
		_exit(127);
	}

	close(errorPipe[1]);
	char output[512];
	size_t length = 0;
	ssize_t bytes;
	while ((bytes = read(errorPipe[0], output + length,
			sizeof(output) - 1 - length)) != 0) {
		if (bytes < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		length += bytes;
		if (length == sizeof(output) - 1) {
			// keep the start, which has the error, and drain the rest
			char drain[256];
			while (read(errorPipe[0], drain, sizeof(drain)) > 0)
				;
			break;
		}
	}
	output[length] = '\0';
	close(errorPipe[0]);

	int status;
	while (waitpid(child, &status, 0) < 0) {
		if (errno != EINTR)
			return -errno;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return B_OK;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
		return B_DEVICE_NOT_FOUND;

	// "mount error(13): Permission denied" carries the kernel's errno,
	// which tells a wrong password from a server that is not up yet
	const char* found = strstr(output, "mount error(");
	if (found != NULL) {
		int code = atoi(found + strlen("mount error("));
		if (code > 0)
			return -code;
	}
	return B_ERROR;
}


//	#pragma mark -


NetworkShares::NetworkShares()
	:
	fLock("network shares"),
	fShares(4),
	fJobs(4),
	fNextID(1),
	fWorker(-1),
	fWakeUp(-1),
	fRetryUntil(0)
{
}


NetworkShares::~NetworkShares()
{
	if (fWakeUp >= 0)
		delete_sem(fWakeUp);
	if (fWorker >= 0)
		wait_for_thread(fWorker, NULL);
}


status_t
NetworkShares::Init()
{
	_ReadSettings();

	fWakeUp = create_sem(0, "network shares jobs");
	if (fWakeUp < 0)
		return fWakeUp;

	fWorker = spawn_thread(&_WorkerEntry, "network shares", B_LOW_PRIORITY,
		this);
	if (fWorker < 0)
		return fWorker;

	status_t status = resume_thread(fWorker);
	if (status == B_OK && !fShares.IsEmpty())
		_AddJob(kJobCheckPasswords, -1, NULL);

	return status;
}


void
NetworkShares::MountAtStartup()
{
	AutoLocker<BLocker> locker(fLock);

	bool any = false;
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		if (share->mountAtStartup) {
			share->wanted = true;
			share->nextTry = 0;
			share->retryDelay = kFirstRetryDelay;
			any = true;
		}
	}

	if (any) {
		fRetryUntil = system_time() + kRetryPeriod;
		locker.Unlock();
		_AddJob(kJobRetry, -1, NULL);
	}
}


/*!	There is a network where there was none, or the other way round: the
	shares that could not be mounted may be within reach now.
*/
void
NetworkShares::NetworkChanged()
{
	AutoLocker<BLocker> locker(fLock);

	bool any = false;
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		if (share->wanted && share->error != B_OK
			&& _IsWorthTryingAgain(share->error)) {
			// Not at once, an interface that is up has no address yet.
			// And sooner only, never later: these can come one after the
			// other for as long as someone is looking for a network.
			const bigtime_t nextTry = system_time() + kFirstRetryDelay;
			if (share->nextTry == 0 || share->nextTry > nextTry)
				share->nextTry = nextTry;
			share->retryDelay = kFirstRetryDelay;
			any = true;
		}
	}

	if (any) {
		fRetryUntil = system_time() + kRetryPeriod;
		locker.Unlock();
		_AddJob(kJobRetry, -1, NULL);
	}
}


/*!	Someone has unmounted \a device. If that is one of the shares, they will
	not want it back the next moment.
*/
void
NetworkShares::VolumeUnmounted(dev_t device)
{
	fs_info info;
	if (fs_stat_dev(device, &info) != B_OK
		|| (info.flags & B_FS_IS_SHARED) == 0) {
		return;
	}

	AutoLocker<BLocker> locker(fLock);

	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);

		BString deviceName;
		deviceName.SetToFormat("//%s/%s", share->server.String(),
			share->share.String());
		if (deviceName == info.device_name
			&& share->name == info.volume_name) {
			share->wanted = false;
			share->mounted = false;
			share->nextTry = 0;
		}
	}
}


/*!	Returns whether \a message was one for the shares. If it was, it is
	answered, now or later.
*/
bool
NetworkShares::HandleMessage(BMessage* message)
{
	switch (message->what) {
		case kGetNetworkShares:
		{
			BMessage reply(kNetworkShareReply);
			reply.AddInt32("request", message->what);
			_GetShares(reply);
			message->SendReply(&reply);
			return true;
		}

		case kSetNetworkShare:
			// asks the key store, which may ask the user
			_AddJob(message->what, -1, be_app->DetachCurrentMessage());
			return true;

		case kRemoveNetworkShare:
		case kMountNetworkShare:
		case kUnmountNetworkShare:
		{
			int32 id;
			if (message->FindInt32("id", &id) != B_OK) {
				_Reply(message, B_BAD_VALUE);
				return true;
			}

			_AddJob(message->what, id, be_app->DetachCurrentMessage());
			return true;
		}
	}

	return false;
}


//	#pragma mark - the list


void
NetworkShares::_GetShares(BMessage& reply)
{
	AutoLocker<BLocker> locker(fLock);

	BObjectList<Share, true> shares(fShares.CountItems());
	for (int32 i = 0; i < fShares.CountItems(); i++)
		shares.AddItem(new Share(*fShares.ItemAt(i)));

	// asking the volumes what they are can take a while
	locker.Unlock();

	for (int32 i = 0; i < shares.CountItems(); i++) {
		const Share* share = shares.ItemAt(i);

		BMessage message;
		_ToMessage(*share, message);
		message.AddBool("has password", share->hasPassword);
		message.AddInt32("error", share->error);

		dev_t device;
		BString mountPoint;
		const bool mounted = _FindVolume(*share, device, mountPoint);
		message.AddBool("mounted", mounted);
		if (mounted) {
			message.AddInt32("device", device);
			message.AddString("mount point", mountPoint);
		}

		reply.AddMessage("share", &message);
	}
}


/*!	Adds the share \a message describes, or changes the one it has the ID
	of. Only the worker does this, and takes shares away.
*/
status_t
NetworkShares::_SetShare(const BMessage& message, int32& _id)
{
	AutoLocker<BLocker> locker(fLock);

	Share previous;
	bool isNew = true;

	int32 id;
	if (message.FindInt32("id", &id) == B_OK && id >= 0) {
		const Share* share = _ShareFor(id);
		if (share == NULL)
			return B_ENTRY_NOT_FOUND;

		previous = *share;
		isNew = false;
	}

	locker.Unlock();

	if (!isNew) {
		dev_t device;
		BString mountPoint;
		previous.mounted = _FindVolume(previous, device, mountPoint);
	}

	Share changed = previous;
	message.FindString("name", &changed.name);
	message.FindString("server", &changed.server);
	message.FindString("share", &changed.share);
	message.FindString("path", &changed.path);
	message.FindString("user", &changed.user);
	message.FindString("domain", &changed.domain);
	message.FindBool("mount at startup", &changed.mountAtStartup);
	message.FindBool("read only", &changed.readOnly);
	message.FindBool("encrypt", &changed.encrypt);

	changed.server.Trim();
	changed.share.Trim();
	changed.path.Trim();
	changed.name.Trim();
	changed.user.Trim();
	changed.domain.Trim();

	// they are written \\server\share elsewhere, and get pasted
	changed.server.RemoveAll("\\");
	changed.server.RemoveAll("/");
	changed.share.ReplaceAll('\\', '/');
	while (changed.share.StartsWith("/"))
		changed.share.Remove(0, 1);
	while (changed.share.EndsWith("/"))
		changed.share.Truncate(changed.share.Length() - 1);

	// what comes after the share is a directory of it
	int32 slash = changed.share.FindFirst('/');
	if (slash >= 0) {
		if (changed.path.IsEmpty()) {
			changed.share.CopyInto(changed.path, slash + 1,
				changed.share.Length());
		}
		changed.share.Truncate(slash);
	}

	if (changed.server.IsEmpty() || changed.share.IsEmpty())
		return B_BAD_VALUE;

	if (changed.name.IsEmpty())
		changed.name = changed.share;
	changed.name.ReplaceAll('/', '-');
	changed.name.Truncate(B_FILE_NAME_LENGTH - 8);

	// A share that is mounted is not what it is mounted as anymore. It can
	// only be had the new way if it lets go of the old one.
	bool mount = false;
	message.FindBool("mount", &mount);

	const char* password;
	const bool newPassword = message.FindString("password", &password)
		== B_OK;

	if (!isNew && previous.mounted) {
		if (newPassword || changed.name != previous.name
			|| changed.server != previous.server
			|| changed.share != previous.share
			|| changed.path != previous.path
			|| changed.user != previous.user
			|| changed.domain != previous.domain
			|| changed.readOnly != previous.readOnly
			|| changed.encrypt != previous.encrypt) {
			status_t error = _Unmount(id);
			if (error != B_OK)
				return error;

			mount = true;
		} else
			mount = false;
	}

	// The password is kept by what it is the password to. When that is
	// another now, it moves along.
	if (newPassword) {
		if (!isNew)
			_SetPassword(previous, NULL);

		status_t error = _SetPassword(changed, password);
		if (error != B_OK)
			return error;

		changed.hasPassword = password[0] != '\0';
	} else if (!isNew && (_KeyIdentifier(changed) != _KeyIdentifier(previous)
			|| changed.user != previous.user)) {
		BString kept;
		if (_GetPassword(previous, kept) == B_OK) {
			_SetPassword(previous, NULL);
			changed.hasPassword = _SetPassword(changed, kept) == B_OK
				&& !kept.IsEmpty();
		}
	}

	locker.Lock();

	Share* share;
	if (isNew) {
		share = new(std::nothrow) Share;
		if (share == NULL || !fShares.AddItem(share)) {
			delete share;
			return B_NO_MEMORY;
		}
		share->id = fNextID++;
	} else {
		share = _ShareFor(id);
		if (share == NULL)
			return B_ENTRY_NOT_FOUND;
	}

	share->name = changed.name;
	share->server = changed.server;
	share->share = changed.share;
	share->path = changed.path;
	share->user = changed.user;
	share->domain = changed.domain;
	share->mountAtStartup = changed.mountAtStartup;
	share->readOnly = changed.readOnly;
	share->encrypt = changed.encrypt;
	share->hasPassword = changed.hasPassword;
	share->error = B_OK;

	_id = share->id;
	status_t error = _WriteSettings();

	locker.Unlock();

	// how that goes is for the share to tell, it was set at any rate
	if (error == B_OK && mount)
		_Mount(_id, true);

	return error;
}


status_t
NetworkShares::_RemoveShare(int32 id)
{
	AutoLocker<BLocker> locker(fLock);

	Share* share = _ShareFor(id);
	if (share == NULL)
		return B_ENTRY_NOT_FOUND;

	_SetPassword(*share, NULL);
	fShares.RemoveItem(share);
		// deletes it

	return _WriteSettings();
}


NetworkShares::Share*
NetworkShares::_ShareFor(int32 id) const
{
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		if (fShares.ItemAt(i)->id == id)
			return fShares.ItemAt(i);
	}
	return NULL;
}


void
NetworkShares::_ToMessage(const Share& share, BMessage& message) const
{
	message.AddInt32("id", share.id);
	message.AddString("name", share.name);
	message.AddString("server", share.server);
	message.AddString("share", share.share);
	message.AddString("path", share.path);
	message.AddString("user", share.user);
	message.AddString("domain", share.domain);
	message.AddBool("mount at startup", share.mountAtStartup);
	message.AddBool("read only", share.readOnly);
	message.AddBool("encrypt", share.encrypt);
}


status_t
NetworkShares::_ReadSettings()
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append(kSettingsFile);

	BMessage settings;
	DriverSettingsMessageAdapter adapter;
	status = adapter.ConvertFromDriverSettings(path.Path(), kSharesTemplate,
		settings);
	if (status != B_OK)
		return status;

	BMessage message;
	for (int32 i = 0; settings.FindMessage("share", i, &message) == B_OK;
			i++) {
		Share* share = new(std::nothrow) Share;
		if (share == NULL)
			return B_NO_MEMORY;

		message.FindInt32("id", &share->id);
		message.FindString("name", &share->name);
		message.FindString("server", &share->server);
		message.FindString("share", &share->share);
		message.FindString("path", &share->path);
		message.FindString("user", &share->user);
		message.FindString("domain", &share->domain);
		message.FindBool("mount_at_startup", &share->mountAtStartup);
		message.FindBool("read_only", &share->readOnly);
		message.FindBool("encrypt", &share->encrypt);

		// the file can be written by hand, and then there is none
		if (share->id < 0 || _ShareFor(share->id) != NULL)
			share->id = fNextID;
		if (share->id >= fNextID)
			fNextID = share->id + 1;

		if (share->server.IsEmpty() || share->share.IsEmpty()
			|| !fShares.AddItem(share)) {
			delete share;
			continue;
		}

		if (share->name.IsEmpty())
			share->name = share->share;
	}

	return B_OK;
}


status_t
NetworkShares::_WriteSettings()
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status != B_OK)
		return status;
	path.Append(kSettingsFile);

	BMessage settings;
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		const Share* share = fShares.ItemAt(i);

		BMessage message;
		message.AddInt32("id", share->id);
		message.AddString("name", share->name);
		message.AddString("server", share->server);
		message.AddString("share", share->share);
		if (!share->path.IsEmpty())
			message.AddString("path", share->path);
		if (!share->user.IsEmpty())
			message.AddString("user", share->user);
		if (!share->domain.IsEmpty())
			message.AddString("domain", share->domain);
		message.AddBool("mount_at_startup", share->mountAtStartup);
		message.AddBool("read_only", share->readOnly);
		message.AddBool("encrypt", share->encrypt);

		settings.AddMessage("share", &message);
	}

	DriverSettingsMessageAdapter adapter;
	return adapter.ConvertToDriverSettings(path.Path(), kSharesTemplate,
		settings);
}


//	#pragma mark - the work


void
NetworkShares::_AddJob(uint32 what, int32 id, BMessage* request)
{
	Job* job = new(std::nothrow) Job;
	if (job != NULL) {
		job->what = what;
		job->id = id;
		job->request = request;

		AutoLocker<BLocker> locker(fLock);
		if (!fJobs.AddItem(job)) {
			delete job;
			job = NULL;
		}
	}

	if (job == NULL) {
		_Reply(request, B_NO_MEMORY);
		delete request;
		return;
	}

	release_sem(fWakeUp);
}


void
NetworkShares::_Reply(BMessage* request, status_t error, int32 id)
{
	if (request == NULL)
		return;

	BMessage reply(kNetworkShareReply);
	reply.AddInt32("request", request->what);
	reply.AddInt32("error", error);
	if (id >= 0) {
		reply.AddInt32("id", id);

		AutoLocker<BLocker> locker(fLock);
		if (const Share* share = _ShareFor(id))
			reply.AddString("name", share->name);
	}
	request->SendReply(&reply);
}


/*static*/ status_t
NetworkShares::_WorkerEntry(void* data)
{
	((NetworkShares*)data)->_Worker();
	return B_OK;
}


void
NetworkShares::_Worker()
{
	while (true) {
		// until there is something to do, or to try again
		bigtime_t wakeUp = B_INFINITE_TIMEOUT;
		{
			AutoLocker<BLocker> locker(fLock);

			if (system_time() < fRetryUntil) {
				for (int32 i = 0; i < fShares.CountItems(); i++) {
					const Share* share = fShares.ItemAt(i);
					if (share->wanted && share->nextTry != 0
						&& share->nextTry < wakeUp) {
						wakeUp = share->nextTry;
					}
				}
			}
		}

		status_t status = acquire_sem_etc(fWakeUp, 1,
			B_ABSOLUTE_TIMEOUT, wakeUp);
		if (status == B_BAD_SEM_ID)
			return;

		while (true) {
			AutoLocker<BLocker> locker(fLock);
			Job* job = fJobs.RemoveItemAt(0);
			locker.Unlock();

			if (job == NULL)
				break;

			status_t error = B_OK;
			switch (job->what) {
				case kSetNetworkShare:
					error = _SetShare(*job->request, job->id);
					break;
				case kJobCheckPasswords:
					_CheckPasswords();
					break;
				case kMountNetworkShare:
					error = _Mount(job->id, true);
					break;
				case kUnmountNetworkShare:
					error = _Unmount(job->id);
					break;
				case kRemoveNetworkShare:
					error = _Unmount(job->id);
					if (error == B_OK)
						error = _RemoveShare(job->id);
					if (error == B_OK)
						job->id = -1;
					break;
				case kJobRetry:
					break;
			}

			_Reply(job->request, error, job->id);
			delete job->request;
			delete job;
		}

		_Retry();
	}
}


/*!	Mounts the shares that are wanted and that it is time for. */
void
NetworkShares::_Retry()
{
	while (true) {
		int32 id = -1;
		{
			AutoLocker<BLocker> locker(fLock);

			if (system_time() >= fRetryUntil)
				return;

			for (int32 i = 0; i < fShares.CountItems(); i++) {
				const Share* share = fShares.ItemAt(i);
				if (share->wanted && !share->mounted
					&& share->nextTry <= system_time()) {
					id = share->id;
					break;
				}
			}
		}

		if (id < 0)
			return;

		_Mount(id, false);
	}
}


/*!	Finds out which shares there is a password for. Those who want to know
	are not to wait for the key store, which may be asking the user whether
	it is to tell at all.
*/
void
NetworkShares::_CheckPasswords()
{
	AutoLocker<BLocker> locker(fLock);

	BObjectList<Share, true> shares(fShares.CountItems());
	for (int32 i = 0; i < fShares.CountItems(); i++)
		shares.AddItem(new Share(*fShares.ItemAt(i)));

	locker.Unlock();

	for (int32 i = 0; i < shares.CountItems(); i++) {
		const status_t status = _HasPassword(*shares.ItemAt(i));
		if (status != B_OK && status != B_ENTRY_NOT_FOUND)
			continue;

		locker.Lock();
		Share* share = _ShareFor(shares.ItemAt(i)->id);
		if (share != NULL)
			share->hasPassword = status == B_OK;
		locker.Unlock();
	}
}


/*!	Mounts the share \a id. \a byUser tells that someone asked for just
	that; they are told how it went, and will not be when it did not work
	while nobody was looking.
*/
status_t
NetworkShares::_Mount(int32 id, bool byUser)
{
	AutoLocker<BLocker> locker(fLock);

	Share* share = _ShareFor(id);
	if (share == NULL)
		return B_ENTRY_NOT_FOUND;

	// the server is waited for without the lock
	const Share copy = *share;
	locker.Unlock();

	status_t error = B_OK;
	dev_t device;
	BString mountPoint;
	bool hasPassword = copy.hasPassword;
	if (!_FindVolume(copy, device, mountPoint)) {
		// No password is one thing, and not getting to know it another:
		// the key store may not be there yet when the system starts, and
		// to try without would have the server say the password is wrong,
		// which is where trying ends.
		BString password;
		if (!copy.user.IsEmpty()) {
			error = _GetPassword(copy, password);
			if (error == B_OK)
				hasPassword = !password.IsEmpty();
			else if (error == B_ENTRY_NOT_FOUND) {
				hasPassword = false;
				error = B_OK;
			}
		}

		if (error == B_OK)
			error = _CreateMountPoint(copy, mountPoint);
		if (error == B_OK) {
			// air/OS mounted its smbfs userlandfs add-on here; Linux has
			// the kernel's SMB client, mounted through mount.cifs.
			error = mount_cifs(copy.server, copy.share, copy.path, copy.user,
				password, copy.domain, copy.readOnly, copy.encrypt,
				mountPoint);
			if (error != B_OK)
				rmdir(mountPoint.String());
		}

		syslog(error == B_OK ? LOG_INFO : LOG_ERR,
			"mount_server: share \"%s\" (//%s/%s): %s\n", copy.name.String(),
			copy.server.String(), copy.share.String(),
			error == B_OK ? "mounted" : strerror(error));
	}

	locker.Lock();

	share = _ShareFor(id);
	if (share == NULL)
		return error;

	share->error = error;
	share->mounted = error == B_OK;
	share->hasPassword = hasPassword;

	if (error == B_OK) {
		share->wanted = true;
		share->nextTry = 0;
		share->retryDelay = kFirstRetryDelay;
		return B_OK;
	}

	if (byUser) {
		// they know, and will try again themselves
		share->wanted = false;
		return error;
	}

	if (_IsWorthTryingAgain(error)) {
		share->nextTry = system_time() + share->retryDelay;
		share->retryDelay = min_c(share->retryDelay * 2, kMaxRetryDelay);
	} else {
		share->wanted = false;
		_Notify(copy, error);
	}

	return error;
}


status_t
NetworkShares::_Unmount(int32 id)
{
	AutoLocker<BLocker> locker(fLock);

	Share* share = _ShareFor(id);
	if (share == NULL)
		return B_ENTRY_NOT_FOUND;

	share->wanted = false;
	share->mounted = false;
	share->nextTry = 0;

	const Share copy = *share;
	locker.Unlock();

	dev_t device;
	BString mountPoint;
	if (!_FindVolume(copy, device, mountPoint))
		return B_OK;

	status_t error = B_OK;
	if (fs_unmount_volume(mountPoint.String(), 0) != B_OK)
		error = errno;

	if (error == B_OK) {
		// the directory was made for it, in the root file system
		if (dev_for_path(mountPoint.String()) == dev_for_path("/"))
			rmdir(mountPoint.String());
	}

	return error;
}


//	#pragma mark - helpers


/*!	Looks for the volume that \a share is mounted as. The mount server may
	have been started anew since it mounted it, so this goes by what the
	volumes say they are.
*/
/*static*/ bool
NetworkShares::_FindVolume(const Share& share, dev_t& _device,
	BString& _mountPoint)
{
	// what the kernel's SMB client shows as the source: the UNC name
	// mount.cifs was given, without a port
	BString deviceName;
	deviceName.SetToFormat("//%s/%s", server_host(share.server).String(),
		share.share.String());
	if (!share.path.IsEmpty()) {
		BString path(share.path);
		path.ReplaceAll('\\', '/');
		while (path.StartsWith("/"))
			path.Remove(0, 1);
		if (!path.IsEmpty())
			deviceName << "/" << path;
	}

	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK) {
		fs_info info;
		if (fs_stat_dev(volume.Device(), &info) != B_OK)
			continue;

		// The volume is named after its mount point, which is the share's
		// name, or that name and a number (_CreateMountPoint()).
		BString volumeName(info.volume_name);
		if ((info.flags & B_FS_IS_SHARED) == 0
			|| (strcmp(info.fsh_name, "cifs") != 0
				&& strcmp(info.fsh_name, "smb3") != 0)
			|| deviceName != info.device_name
			|| (volumeName != share.name
				&& !volumeName.StartsWith(BString(share.name) << " "))) {
			continue;
		}

		BDirectory root;
		BPath path;
		if (volume.GetRootDirectory(&root) != B_OK
			|| path.SetTo(&root, ".") != B_OK) {
			continue;
		}

		_device = volume.Device();
		_mountPoint = path.Path();
		return true;
	}

	return false;
}


/*!	Makes the directory to mount \a share at: in the root directory and with
	the name of the share, as the disks have it, or as close to that as what
	is there already allows.
*/
/*static*/ status_t
NetworkShares::_CreateMountPoint(const Share& share, BString& _path)
{
	const dev_t rootDevice = dev_for_path("/");

	for (int32 i = 1; i < 100; i++) {
		BString path("/");
		path << share.name;
		if (i > 1)
			path << " " << i;

		if (mkdir(path.String(), 0755) == 0) {
			_path = path;
			return B_OK;
		}
		if (errno != B_FILE_EXISTS)
			return errno;

		// one that was left over will do
		struct stat st;
		if (lstat(path.String(), &st) == 0 && S_ISDIR(st.st_mode)
			&& st.st_dev == rootDevice
			&& dev_for_path(path.String()) == rootDevice
			&& BDirectory(path.String()).CountEntries() == 0) {
			_path = path;
			return B_OK;
		}
	}

	return B_FILE_EXISTS;
}


/*static*/ BString
NetworkShares::_KeyIdentifier(const Share& share)
{
	BString identifier;
	identifier.SetToFormat("smb://%s/%s", share.server.String(),
		share.share.String());
	return identifier;
}


/*static*/ status_t
NetworkShares::_GetPassword(const Share& share, BString& _password)
{
	BKeyStore keyStore;
	BPasswordKey key;
	status_t status = keyStore.GetKey(B_KEY_TYPE_PASSWORD,
		_KeyIdentifier(share).String(), share.user.String(), false, key);
	if (status != B_OK)
		return status;

	_password = key.Password();
	return B_OK;
}


/*!	Whether there is a password for \a share: \c B_OK if so,
	\c B_ENTRY_NOT_FOUND if not, and what kept the key store from telling
	otherwise.
*/
/*static*/ status_t
NetworkShares::_HasPassword(const Share& share)
{
	BString password;
	status_t status = _GetPassword(share, password);
	if (status == B_OK && password.IsEmpty())
		return B_ENTRY_NOT_FOUND;
	return status;
}


/*!	Stores \a password as the one for \a share, or removes the one that is
	stored if it is \c NULL.
*/
/*static*/ status_t
NetworkShares::_SetPassword(const Share& share, const char* password)
{
	BKeyStore keyStore;

	BPasswordKey old;
	if (keyStore.GetKey(B_KEY_TYPE_PASSWORD, _KeyIdentifier(share).String(),
			share.user.String(), false, old) == B_OK) {
		keyStore.RemoveKey(old);
	}

	if (password == NULL || password[0] == '\0')
		return B_OK;

	BPasswordKey key(password, B_KEY_PURPOSE_NETWORK,
		_KeyIdentifier(share).String(), share.user.String());
	return keyStore.AddKey(key);
}


/*!	Whether \a error is one that goes away by itself: the network not being
	there yet, the server not being up. The wrong password is not.
*/
/*static*/ bool
NetworkShares::_IsWorthTryingAgain(status_t error)
{
	switch (error) {
		case B_PERMISSION_DENIED:
		case B_NOT_ALLOWED:
		case B_ENTRY_NOT_FOUND:
		case B_NOT_A_DIRECTORY:
		case B_BAD_VALUE:
		case B_NO_MEMORY:
		case B_FILE_EXISTS:
		case B_DEVICE_NOT_FOUND:
			// cifs-utils (mount.cifs) is not installed
		case B_NAME_NOT_FOUND:
			return false;
	}

	return true;
}


/*static*/ void
NetworkShares::_Notify(const Share& share, status_t error)
{
	BString content;
	content.SetToFormat(B_TRANSLATE("\"%s\" on %s could not be mounted: %s"),
		share.name.String(), share.server.String(), strerror(error));

	BNotification notification(B_ERROR_NOTIFICATION);
	notification.SetGroup(B_TRANSLATE("Network shares"));
	notification.SetTitle(share.name);
	notification.SetContent(content);
	notification.SetMessageID(BString("network-share-") << share.id);
	notification.Send();
}
