/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "Updater.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <Directory.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceTypes.h>
#include <DiskDeviceVisitor.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <OS.h>
#include <Partition.h>
#include <Path.h>
#include <Roster.h>
#include <Url.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_info.h>
#include <package/CommitTransactionResult.h>
#include <package/PackageInfo.h>
#include <package/PackageVersion.h>

#include <AutoDeleter.h>
#include <SHA256.h>
#include <airos_boot_control.h>
#include <package/ActivationTransaction.h>
#include <package/DaemonClient.h>

#include "Constants.h"
#include "PiBoot.h"
#include "Settings.h"


using namespace BPackageKit;
using BPackageKit::BPrivate::BActivationTransaction;
using BPackageKit::BPrivate::BDaemonClient;


static const char* const kShaCacheFile = kStateDirectory "/sha256-cache";


UpdaterListener::~UpdaterListener()
{
}


UpdatePlan::UpdatePlan()
	:
	activate(20),
	downloadBytes(0),
	copyBytes(0)
{
}


// #pragma mark - helpers


static bool
file_exists(const char* path)
{
	struct stat st;
	return lstat(path, &st) == 0;
}


static status_t
copy_file(const char* from, const char* to, UpdaterListener* listener,
	off_t& done, off_t total, int32* cancelled)
{
	BFile source(from, B_READ_ONLY);
	BFile target(to, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t error = source.InitCheck();
	if (error == B_OK)
		error = target.InitCheck();
	if (error != B_OK)
		return error;

	const size_t kBufferSize = 1024 * 1024;
	char* buffer = (char*)malloc(kBufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;
	MemoryDeleter bufferDeleter(buffer);

	for (;;) {
		if (cancelled != NULL && atomic_get(cancelled) != 0)
			return B_CANCELED;
		ssize_t bytesRead = source.Read(buffer, kBufferSize);
		if (bytesRead < 0)
			return bytesRead;
		if (bytesRead == 0)
			break;
		ssize_t written = target.Write(buffer, bytesRead);
		if (written != bytesRead)
			return written < 0 ? written : B_IO_ERROR;
		done += bytesRead;
		if (listener != NULL && total > 0)
			listener->UpdaterProgress((float)done / total);
	}
	return target.Sync();
}


static off_t
free_space(const char* path)
{
	struct stat st;
	fs_info info;
	if (stat(path, &st) != 0 || fs_stat_dev(st.st_dev, &info) != 0)
		return -1;
	return info.free_blocks * info.block_size;
}


static BString
time_stamp()
{
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char buffer[32];
	strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
	return buffer;
}


// #pragma mark - Updater


Updater::Updater(UpdaterListener* listener)
	:
	fListener(listener),
	fCancelled(0)
{
}


Updater::~Updater()
{
}


/*static*/ void
Updater::Log(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	BString line;
	line.SetToFormatVarArgs(format, args);
	va_end(args);

	fprintf(stderr, "SystemUpdater: %s\n", line.String());

	SystemState::EnsureDirectory(kStateDirectory);
	if (FILE* file = fopen(kLogFile, "a")) {
		fprintf(file, "%s %s\n", time_stamp().String(), line.String());
		fclose(file);
	}
}


void
Updater::_Status(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	BString text;
	text.SetToFormatVarArgs(format, args);
	va_end(args);
	if (fListener != NULL)
		fListener->UpdaterStatus(text);
}


void
Updater::_Progress(float fraction)
{
	if (fListener != NULL)
		fListener->UpdaterProgress(fraction);
}


void
Updater::Cancel()
{
	atomic_set(&fCancelled, 1);
	fFetcher.Cancel();
}


void
Updater::Reset()
{
	atomic_set(&fCancelled, 0);
	fFetcher.Reset();
}


/*static*/ BString
Updater::PackageNameOf(const char* fileName)
{
	// "<name>-<version>-<revision>-<architecture>.hpkg"
	BString name(fileName);
	int32 dash = name.FindFirst('-');
	if (dash > 0)
		name.Truncate(dash);
	else if (name.EndsWith(".hpkg"))
		name.Truncate(name.Length() - 5);
	return name;
}


/*static*/ BString
Updater::VersionOf(const char* fileName)
{
	// "<name>-<version>-<revision>-<architecture>.hpkg" -> "<version>-<revision>"
	BString version(fileName);
	if (!version.EndsWith(".hpkg"))
		return BString();
	version.Truncate(version.Length() - 5);
	int32 first = version.FindFirst('-');
	int32 last = version.FindLast('-');
	if (first < 0 || last <= first)
		return BString();
	version.Truncate(last);
	version.Remove(0, first + 1);
	return version;
}


/*static*/ BString
Updater::FormatSize(off_t size)
{
	BString text;
	if (size >= 1024LL * 1024 * 1024)
		text.SetToFormat("%.1f GiB", size / (1024.0 * 1024 * 1024));
	else if (size >= 1024 * 1024)
		text.SetToFormat("%.1f MiB", size / (1024.0 * 1024));
	else if (size >= 1024)
		text.SetToFormat("%.0f KiB", size / 1024.0);
	else
		text.SetToFormat("%" B_PRIdOFF " bytes", size);
	return text;
}


/*static*/ status_t
Updater::ComputeSha256(const char* path, BString& _sha256,
	UpdaterListener* listener, int32* cancelled)
{
	BFile file(path, B_READ_ONLY);
	status_t error = file.InitCheck();
	if (error != B_OK)
		return error;
	off_t size = 0;
	file.GetSize(&size);

	const size_t kBufferSize = 1024 * 1024;
	char* buffer = (char*)malloc(kBufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;
	MemoryDeleter bufferDeleter(buffer);

	SHA256 sha;
	off_t done = 0;
	for (;;) {
		if (cancelled != NULL && atomic_get(cancelled) != 0)
			return B_CANCELED;
		ssize_t bytesRead = file.Read(buffer, kBufferSize);
		if (bytesRead < 0)
			return bytesRead;
		if (bytesRead == 0)
			break;
		sha.Update(buffer, bytesRead);
		done += bytesRead;
		if (listener != NULL && size > 0)
			listener->UpdaterProgress((float)done / size);
	}

	const uint8* digest = sha.Digest();
	_sha256.Truncate(0);
	for (size_t i = 0; i < SHA_DIGEST_LENGTH; i++) {
		char hex[3];
		snprintf(hex, sizeof(hex), "%02x", digest[i]);
		_sha256 << hex;
	}
	return B_OK;
}


/*!	The SHA-256 of an installed package, remembered by its node, size and
	modification time, so that each check reads only what changed.
*/
status_t
Updater::_InstalledSha256(const char* fileName, BString& _sha256)
{
	BString path(kPackagesDirectory);
	path << '/' << fileName;
	struct stat st;
	if (stat(path, &st) != 0)
		return errno;

	BString key;
	key.SetToFormat("%" B_PRIdINO " %" B_PRIdOFF " %" B_PRIdTIME " %s",
		st.st_ino, st.st_size, st.st_mtime, fileName);

	BString cache;
	BFile cacheFile(kShaCacheFile, B_READ_ONLY);
	if (cacheFile.InitCheck() == B_OK) {
		off_t size;
		if (cacheFile.GetSize(&size) == B_OK && size < 4 * 1024 * 1024) {
			char* buffer = cache.LockBuffer(size + 1);
			ssize_t bytesRead = cacheFile.ReadAt(0, buffer, size);
			cache.UnlockBuffer(bytesRead > 0 ? bytesRead : 0);
		}
	}
	BStringList lines;
	cache.Split("\n", true, lines);
	for (int32 i = 0; i < lines.CountStrings(); i++) {
		const BString& line = lines.StringAt(i);
		// "<sha256> <key>"
		if (line.Length() > 65 && line.FindFirst(key) == 65) {
			line.CopyInto(_sha256, 0, 64);
			return B_OK;
		}
	}

	status_t error = ComputeSha256(path, _sha256, NULL, &fCancelled);
	if (error != B_OK)
		return error;

	if (FILE* file = fopen(kShaCacheFile, "a")) {
		fprintf(file, "%s %s\n", _sha256.String(), key.String());
		fclose(file);
	}
	return B_OK;
}


status_t
Updater::LoadInstalledRelease(Release& _release) const
{
	return _release.Load(kReleaseFile);
}


BString
Updater::Target() const
{
	Release release;
	if (LoadInstalledRelease(release) == B_OK && !release.Target().IsEmpty())
		return release.Target();
#if defined(__x86_64__)
	return "x86_64";
#elif defined(__aarch64__)
	return "arm64";
#else
	return "unknown";
#endif
}


BString
Updater::FeedURL() const
{
	if (!fFeedOverride.IsEmpty())
		return fFeedOverride;

	Settings settings;
	settings.Load();
	if (!settings.feed.IsEmpty())
		return settings.feed;

	Release release;
	if (LoadInstalledRelease(release) == B_OK && !release.Feed().IsEmpty())
		return release.Feed();

	BString url(kDefaultFeedBase);
	url << Target() << '/';
	return url;
}


status_t
Updater::Check(Feed& _feed, BString& _error)
{
	BString url = FeedURL();
	_Status("Asking %s…", url.String());
	if (url.StartsWith("https://"))
		_CheckClockForSecureFeed(url);
	status_t error = _feed.Fetch(url, fFetcher, _error);
	if (error == B_OK)
		_CheckClock();
	if (error == B_OK) {
		BString when;
		when << time_stamp() << "\n";
		SystemState::EnsureDirectory(kStateDirectory);
		SystemState::WriteFileAtomically(kCheckFile, when);
		if (!_feed.Target().IsEmpty() && _feed.Target() != Target()) {
			_error.SetToFormat("the feed at %s is for %s, this is %s",
				url.String(), _feed.Target().String(), Target().String());
			return B_MISMATCHED_VALUES;
		}
	}
	return error;
}


bool
Updater::IsNewer(const FeedBuild& build) const
{
	Release installed;
	if (LoadInstalledRelease(installed) != B_OK)
		return true;
	return Release::CompareIds(build.id, installed.Id()) > 0;
}


status_t
Updater::FetchRelease(const FeedBuild& build, Release& _release,
	BString& _error)
{
	BString text;
	status_t error = fFetcher.FetchText(build.manifest, text, _error);
	if (error != B_OK)
		return error;

	if (!build.manifestSha256.IsEmpty()) {
		SHA256 sha;
		sha.Update(text.String(), text.Length());
		const uint8* digest = sha.Digest();
		BString hex;
		for (size_t i = 0; i < SHA_DIGEST_LENGTH; i++) {
			char byte[3];
			snprintf(byte, sizeof(byte), "%02x", digest[i]);
			hex << byte;
		}
		if (hex != build.manifestSha256) {
			_error.SetToFormat("%s does not match the feed's checksum",
				build.manifest.String());
			return B_BAD_DATA;
		}
	}

	BMessage object;
	error = json::Parse(text, object);
	if (error == B_OK)
		error = _release.SetTo(object);
	if (error != B_OK) {
		_error.SetToFormat("%s is not a valid release description",
			build.manifest.String());
		return error;
	}
	if (_release.Id() != build.id || _release.Target() != Target()) {
		_error.SetToFormat("%s describes %s for %s, not %s for %s",
			build.manifest.String(), _release.Id().String(),
			_release.Target().String(), build.id.String(), Target().String());
		return B_MISMATCHED_VALUES;
	}

	// The package addresses are relative to the manifest.
	BMessage resolved = _release.Object();
	BMessage packages;
	if (json::Object(resolved, "packages", packages)) {
		BMessage entry;
		for (int32 i = 0; json::At(packages, i, entry); i++) {
			BString url = Feed::Resolve(build.manifest,
				json::String(entry, "url"));
			entry.RemoveName("url");
			entry.AddString("url", url);
			BString name;
			name << i;
			packages.ReplaceMessage(name, &entry);
		}
		resolved.ReplaceMessage("packages", &packages);
	}
	BMessage bootFiles;
	if (json::Object(resolved, "boot_files", bootFiles)) {
		BMessage entry;
		for (int32 i = 0; json::At(bootFiles, i, entry); i++) {
			BString url = Feed::Resolve(build.manifest,
				json::String(entry, "url"));
			entry.RemoveName("url");
			entry.AddString("url", url);
			BString name;
			name << i;
			bootFiles.ReplaceMessage(name, &entry);
		}
		resolved.ReplaceMessage("boot_files", &bootFiles);
	}
	return _release.SetTo(resolved);
}


status_t
Updater::_LoadReleaseRecord(const char* id, Release& _release) const
{
	BString path(kReleasesDirectory);
	path << '/' << id << ".json";
	return _release.Load(path);
}


/*!	The record of the release whose airos_release package has \a version.
*/
status_t
Updater::_LoadReleaseRecordForVersion(const BString& version,
	Release& _release) const
{
	if (version.IsEmpty())
		return B_BAD_VALUE;
	status_t error = B_ENTRY_NOT_FOUND;
	if (DIR* dir = opendir(kReleasesDirectory)) {
		while (dirent* entry = readdir(dir)) {
			BString name(entry->d_name);
			if (!name.EndsWith(".json"))
				continue;
			name.Truncate(name.Length() - 5);
			if (Release::VersionForId(name) == version
				&& _LoadReleaseRecord(name, _release) == B_OK) {
				error = B_OK;
				break;
			}
		}
		closedir(dir);
	}
	return error;
}


status_t
Updater::_SaveReleaseRecord(const Release& release)
{
	status_t error = SystemState::EnsureDirectory(kReleasesDirectory);
	if (error != B_OK)
		return error;

	// as JSON again, so the record reads like the feed's update.json
	BString text("{\n");
	text << " \"format\": 1,\n";
	text << " \"id\": " << json::Quote(release.Id()) << ",\n";
	text << " \"target\": " << json::Quote(release.Target()) << ",\n";
	text << " \"arch\": " << json::Quote(release.Architecture()) << ",\n";
	text << " \"built\": " << json::Quote(release.Built()) << ",\n";
	text << " \"version\": " << json::Quote(release.Version()) << ",\n";
	text << " \"haiku\": {\"revision\": "
		<< json::Quote(release.HaikuRevision()) << ", \"commit\": "
		<< json::Quote(release.HaikuCommit()) << "},\n";
	text << " \"packages\": [\n";
	for (int32 i = 0; const ReleasePackage* package
			= release.Packages().ItemAt(i); i++) {
		text << "  {\"name\": " << json::Quote(package->name)
			<< ", \"version\": " << json::Quote(package->version)
			<< ", \"file\": " << json::Quote(package->file)
			<< ", \"sha256\": " << json::Quote(package->sha256)
			<< ", \"size\": " << package->size
			<< (package->optional ? ", \"optional\": true" : "");
		if (!package->equivalent.IsEmpty()) {
			text << ", \"equivalent\": [";
			for (int32 j = 0; j < package->equivalent.CountStrings(); j++) {
				text << (j > 0 ? ", " : "")
					<< json::Quote(package->equivalent.StringAt(j));
			}
			text << "]";
		}
		text << "}"
			<< (i + 1 < release.Packages().CountItems() ? ",\n" : "\n");
	}
	text << " ],\n \"payload\": {\"links\": [\n";
	for (int32 i = 0; const PayloadLink* link
			= release.PayloadLinks().ItemAt(i); i++) {
		text << "  {\"path\": " << json::Quote(link->path) << ", \"type\": "
			<< json::Quote(link->type);
		if (!link->target.IsEmpty())
			text << ", \"target\": " << json::Quote(link->target);
		text << "}"
			<< (i + 1 < release.PayloadLinks().CountItems() ? ",\n" : "\n");
	}
	text << " ]}\n}\n";

	BString path(kReleasesDirectory);
	path << '/' << release.Id() << ".json";
	return SystemState::WriteFileAtomically(path, text);
}


/*!	The time the installed release was built, or 0.
*/
static time_t
installed_build_time(const Release& release)
{
	struct tm tm = {};
	if (sscanf(release.Built().String(), "%d-%d-%dT%d:%d:%d", &tm.tm_year,
			&tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
		return 0;
	return utc_time(tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min,
		tm.tm_sec);
}


/*!	Package states are named after the time and the boot loader orders them
	by name, so they need a clock that is about right. Boards without a
	battery-backed clock (the Pi, the Cubie) start in 1970 until they reach a
	time server: a clock before the installed release's own build is
	certainly wrong, and the build server's is better.
*/
void
Updater::_CheckClock()
{
	Release installed;
	time_t built = LoadInstalledRelease(installed) == B_OK
		? installed_build_time(installed) : 0;
	time_t server = fFetcher.ServerTime();
	time_t now = time(NULL);
	if (built > 0 && now < built && server > now + 3600) {
		set_real_time_clock(server);
		Log("the clock was before this release was built; set it from the "
			"build server's");
	}
}


/*!	A board without a battery-backed clock starts in 1970, when no
	certificate is valid yet: before an HTTPS feed can answer, the clock is
	set from its host's answer (Fetcher::FetchServerTime()).
*/
void
Updater::_CheckClockForSecureFeed(const BString& url)
{
	Release installed;
	time_t built = LoadInstalledRelease(installed) == B_OK
		? installed_build_time(installed) : 0;
	time_t now = time(NULL);
	if (built <= 0 || now >= built)
		return;
	time_t server = fFetcher.FetchServerTime(url);
	if (server > now + 3600) {
		set_real_time_clock(server);
		Log("the clock was before this release was built; set it from %s's",
			BUrl(url.String(), true).Host().String());
	} else {
		Log("the clock is before this release was built, and %s did not "
			"tell the time", BUrl(url.String(), true).Host().String());
	}
}


status_t
Updater::CheckPreconditions(const SystemState& state, BString& _error) const
{
	// TODO(linux backend): System Updater moves air/OS's packagefs package
	// states and its boot loader's trial starts. A system on Linux keeps its
	// software in Debian packages (apt, the vos repository) and has neither,
	// so it updates nothing there.
	if (access(kAdminDirectory "/" kActivationFile, F_OK) != 0) {
		_error = "This system has no air/OS package states to update: its "
			"software comes in Debian packages. Update it with the package "
			"manager.";
		return B_NOT_SUPPORTED;
	}

	Release installed;
	time_t built = LoadInstalledRelease(installed) == B_OK
		? installed_build_time(installed) : 0;
	if (built > 0 && time(NULL) < built) {
		_error = "The clock is wrong: it is earlier than when this version "
			"of air/OS was made. Set the date and time in the Time "
			"preferences first, since air/OS names its versions by time.";
		return B_BAD_VALUE;
	}

	if (!state.RunningLatest()) {
		_error = "air/OS is running an older version. Restart into the "
			"current version, or go back to this one, first.";
		return B_NOT_ALLOWED;
	}
	if (state.RestartPending()) {
		_error = "Changes to the system wait for a restart. Restart first.";
		return B_BUSY;
	}
	TrialInfo trial;
	if (SystemState::ReadTrial(trial) == B_OK) {
		_error = "The last update has not been confirmed yet: it is confirmed "
			"once the desktop has run for half a minute.";
		return B_BUSY;
	}
	return B_OK;
}


status_t
Updater::PlanUpdate(const Release& release, const SystemState& state,
	UpdatePlan& _plan, BString& _error, const Feed* feed)
{
	_plan.release = release;
	_plan.goBackState.Truncate(0);
	_plan.activate.MakeEmpty();
	_plan.deactivate.MakeEmpty();
	_plan.kept.MakeEmpty();
	_plan.downloadBytes = 0;
	_plan.copyBytes = 0;

	const InstalledVersion* latest = state.Latest();
	if (latest == NULL) {
		_error = "cannot read the installed packages";
		return B_ERROR;
	}

	// What the installed release had, to remove what the new one drops. A
	// record made from the installed packages (a system installed from an
	// image) cannot tell the release's packages from the user's: then the
	// feed's description of that release is asked for, and without it
	// nothing is dropped.
	Release installed;
	BStringList previousNames;
	BStringList previousVersions;
	if (LoadInstalledRelease(installed) == B_OK) {
		Release record;
		bool haveRecord = _LoadReleaseRecord(installed.Id(), record) == B_OK
			&& !record.IsBootstrap();
		const FeedBuild* build = feed != NULL
			? feed->FindBuild(installed.Id()) : NULL;
		if (!haveRecord && build != NULL) {
			BString error;
			if (FetchRelease(*build, record, error) == B_OK) {
				_SaveReleaseRecord(record);
				haveRecord = true;
			}
		}
		if (haveRecord) {
			for (int32 i = 0; const ReleasePackage* package
					= record.Packages().ItemAt(i); i++) {
				previousNames.Add(package->name);
				previousVersions.Add(package->version);
			}
		}
	}

	BStringList installedNames;
	for (int32 i = 0; i < latest->packages.CountStrings(); i++)
		installedNames.Add(PackageNameOf(latest->packages.StringAt(i)));

	// A release may carry two files of one package (an image's own build of
	// a library beside the one a dependency brought in); only one of them can
	// be active, the highest version.
	BStringList skippedFiles;
	for (int32 i = 0; const ReleasePackage* package
			= release.Packages().ItemAt(i); i++) {
		for (int32 j = 0; const ReleasePackage* other
				= release.Packages().ItemAt(j); j++) {
			if (other == package || other->name != package->name)
				continue;
			BPackageVersion version, otherVersion;
			if (version.SetTo(package->version) == B_OK
				&& otherVersion.SetTo(other->version) == B_OK
				&& (version.Compare(otherVersion) < 0
					|| (version.Compare(otherVersion) == 0 && j < i))) {
				skippedFiles.Add(package->file);
				break;
			}
		}
	}

	// A package of the user's own that is newer than the release's stays,
	// rather than going back to an older version that what the user built on
	// it may not work with (Electron apps on a newer Aurora): one updated from
	// HaikuDepot, or any of a system no release installed (converted in
	// place). What the installed release brought follows the release, and so
	// do the system's own packages.
	BStringList userNewer;
	for (int32 i = 0; i < latest->packages.CountStrings(); i++) {
		const BString& file = latest->packages.StringAt(i);
		BString name = PackageNameOf(file);
		if (name.StartsWith("haiku") || name.StartsWith("airos_")
			|| name == "makefile_engine") {
			continue;
		}
		BString installedVersion = VersionOf(file);
		BPackageVersion version;
		if (installedVersion.IsEmpty() || version.SetTo(installedVersion) != B_OK)
			continue;
		int32 previous = previousNames.IndexOf(name);
		if (previous >= 0
			&& previousVersions.StringAt(previous) == installedVersion) {
			continue;
		}
		bool newer = false;
		for (int32 j = 0; const ReleasePackage* package
				= release.Packages().ItemAt(j); j++) {
			BPackageVersion releaseVersion;
			if (package->name != name || skippedFiles.HasString(package->file)
				|| releaseVersion.SetTo(package->version) != B_OK) {
				continue;
			}
			newer = version.Compare(releaseVersion) > 0;
			break;
		}
		if (newer)
			userNewer.Add(name);
	}

	BStringList releaseNames;
	BStringList releaseFiles;
	for (int32 i = 0; const ReleasePackage* package
			= release.Packages().ItemAt(i); i++) {
		if (package->optional && !installedNames.HasString(package->name))
			continue;
		if (skippedFiles.HasString(package->file)
			|| userNewer.HasString(package->name)) {
			continue;
		}
		releaseNames.Add(package->name);
		releaseFiles.Add(package->file);
	}

	// packages of the release this system does not have, or not as built
	int32 count = release.Packages().CountItems();
	for (int32 i = 0; i < count; i++) {
		const ReleasePackage* package = release.Packages().ItemAt(i);
		if ((package->optional && !installedNames.HasString(package->name))
			|| skippedFiles.HasString(package->file)
			|| userNewer.HasString(package->name)) {
			continue;
		}
		_Status("Comparing %s (%" B_PRId32 " of %" B_PRId32 ")…",
			package->file.String(), i + 1, count);
		_Progress((float)i / count);
		if (atomic_get(&fCancelled) != 0) {
			_error = "cancelled";
			return B_CANCELED;
		}

		if (latest->packages.HasString(package->file)) {
			BString sha;
			if (_InstalledSha256(package->file, sha) == B_OK
				&& (sha == package->sha256
					|| package->equivalent.HasString(sha))) {
				continue;
			}
			// the same file name with other contents: replace it
			_plan.deactivate.Add(package->file);
		}

		PlannedPackage* planned = new PlannedPackage;
		planned->file = package->file;
		planned->sha256 = package->sha256;
		planned->url = package->url;
		planned->size = package->size;
		planned->download = true;
		planned->copy = false;
		_plan.activate.AddItem(planned);
		if (package->size > 0)
			_plan.downloadBytes += package->size;
	}

	// installed packages the release replaces or dropped
	for (int32 i = 0; i < latest->packages.CountStrings(); i++) {
		const BString& file = latest->packages.StringAt(i);
		if (releaseFiles.HasString(file))
			continue;
		BString name = PackageNameOf(file);
		if (userNewer.HasString(name))
			_plan.kept.Add(file);
		else if (releaseNames.HasString(name) || previousNames.HasString(name))
			_plan.deactivate.Add(file);
		else
			_plan.kept.Add(file);
	}

	_Progress(-1);
	return B_OK;
}


status_t
Updater::PlanGoBack(const InstalledVersion& version, const SystemState& state,
	UpdatePlan& _plan, BString& _error)
{
	_plan.goBackState = version.state;
	_plan.activate.MakeEmpty();
	_plan.deactivate.MakeEmpty();
	_plan.kept.MakeEmpty();
	_plan.downloadBytes = 0;
	_plan.copyBytes = 0;

	const InstalledVersion* latest = state.Latest();
	if (latest == NULL || version.latest) {
		_error = "that is the current version already";
		return B_BAD_VALUE;
	}
	if (!version.bootable) {
		_error = "that version's system package is gone";
		return B_ENTRY_NOT_FOUND;
	}

	// what is known of that release: its record, else its package list
	Release record;
	if (_LoadReleaseRecordForVersion(version.releaseVersion, record) == B_OK)
		_plan.release = record;
	else
		_plan.release = Release();

	BStringList targetNames;
	for (int32 i = 0; i < version.packages.CountStrings(); i++)
		targetNames.Add(PackageNameOf(version.packages.StringAt(i)));

	// The latest state's release's own packages, which may go. That is not
	// the release that runs when going back from an older state.
	BStringList latestReleaseNames;
	Release latestRecord;
	if (_LoadReleaseRecordForVersion(latest->releaseVersion, latestRecord)
			== B_OK) {
		for (int32 i = 0; const ReleasePackage* package
				= latestRecord.Packages().ItemAt(i); i++) {
			latestReleaseNames.Add(package->name);
		}
	}

	for (int32 i = 0; i < version.packages.CountStrings(); i++) {
		const BString& file = version.packages.StringAt(i);
		BPath path;
		if (state.FindPackageFile(version, file, path) != B_OK) {
			_error.SetToFormat("%s of that version is gone", file.String());
			return B_ENTRY_NOT_FOUND;
		}
		BString source(path.Path());
		BString inPackages(kPackagesDirectory);
		inPackages << '/' << file;
		if (source == inPackages)
			continue;
			// the same file is installed now

		if (latest->packages.HasString(file) && file_exists(inPackages))
			_plan.deactivate.Add(file);

		PlannedPackage* planned = new PlannedPackage;
		planned->file = file;
		planned->source = source;
		planned->size = 0;
		planned->download = false;
		planned->copy = true;
		struct stat st;
		if (stat(source, &st) == 0) {
			planned->size = st.st_size;
			_plan.copyBytes += st.st_size;
		}
		_plan.activate.AddItem(planned);
	}

	for (int32 i = 0; i < latest->packages.CountStrings(); i++) {
		const BString& file = latest->packages.StringAt(i);
		if (version.packages.HasString(file)
			|| _plan.deactivate.HasString(file)) {
			continue;
		}
		// After a package transaction that was cut short, the activation file
		// may name packages that were already moved away.
		BString path(kPackagesDirectory);
		path << '/' << file;
		if (!file_exists(path))
			continue;
		BString name = PackageNameOf(file);
		if (targetNames.HasString(name) || latestReleaseNames.HasString(name))
			_plan.deactivate.Add(file);
		else
			_plan.kept.Add(file);
	}

	return B_OK;
}


status_t
Updater::_VerifyPackage(const char* path, const ReleasePackage& expected,
	BString& _error)
{
	BPackageInfo info;
	status_t error = info.ReadFromPackageFile(path);
	if (error != B_OK) {
		_error.SetToFormat("%s is not a package: %s", expected.file.String(),
			strerror(error));
		return error;
	}
	if (info.Name() != expected.name) {
		_error.SetToFormat("%s holds the package %s, not %s",
			expected.file.String(), info.Name().String(),
			expected.name.String());
		return B_BAD_DATA;
	}
	if (!expected.version.IsEmpty()
		&& info.Version().ToString() != expected.version) {
		_error.SetToFormat("%s holds version %s, not %s",
			expected.file.String(), info.Version().ToString().String(),
			expected.version.String());
		return B_BAD_DATA;
	}
	return B_OK;
}


status_t
Updater::Download(UpdatePlan& plan, BString& _error)
{
	status_t error = SystemState::EnsureDirectory(kDownloadsDirectory);
	if (error != B_OK) {
		_error.SetToFormat("cannot create %s: %s", kDownloadsDirectory,
			strerror(error));
		return error;
	}

	// room for the downloads and their move into place, with a margin
	off_t needed = plan.downloadBytes + plan.copyBytes + 256 * 1024 * 1024;
	off_t available = free_space(kPackagesDirectory);
	if (available >= 0 && available < needed) {
		_error.SetToFormat("The update needs %s of free space on the system "
			"disk; %s are free.", FormatSize(needed).String(),
			FormatSize(available).String());
		return B_DEVICE_FULL;
	}

	struct Progress : DownloadListener {
		Updater*	updater;
		off_t		done;
		off_t		total;
		virtual void DownloadProgress(off_t bytes, off_t)
		{
			if (total > 0)
				updater->_Progress((float)(done + bytes) / total);
		}
	} progress;
	progress.updater = this;
	progress.done = 0;
	progress.total = plan.downloadBytes;

	int32 count = 0;
	for (int32 i = 0; PlannedPackage* package = plan.activate.ItemAt(i); i++) {
		if (package->download)
			count++;
	}

	int32 index = 0;
	for (int32 i = 0; PlannedPackage* package = plan.activate.ItemAt(i); i++) {
		if (!package->download)
			continue;
		index++;
		if (atomic_get(&fCancelled) != 0) {
			_error = "cancelled";
			return B_CANCELED;
		}

		const ReleasePackage* expected = NULL;
		for (int32 j = 0; const ReleasePackage* candidate
				= plan.release.Packages().ItemAt(j); j++) {
			if (candidate->file == package->file) {
				expected = candidate;
				break;
			}
		}
		if (expected == NULL) {
			_error.SetToFormat("%s is not in the release", package->file.String());
			return B_ERROR;
		}

		BString path(kDownloadsDirectory);
		path << '/' << package->sha256.String() << '-' << package->file;
		package->source = path;

		_Status("Downloading %s (%" B_PRId32 " of %" B_PRId32 ", %s)…",
			package->file.String(), index, count,
			FormatSize(package->size).String());

		bool verified = false;
		for (int attempt = 0; attempt < 2 && !verified; attempt++) {
			error = fFetcher.FetchFile(package->url, path, package->size,
				_error, &progress);
			if (error != B_OK) {
				Log("download of %s failed: %s", package->url.String(),
					_error.String());
				return error;
			}

			BString sha;
			error = ComputeSha256(path, sha, NULL, &fCancelled);
			if (error != B_OK) {
				_error.SetToFormat("cannot read %s: %s", path.String(),
					strerror(error));
				return error;
			}
			if (sha == package->sha256)
				verified = true;
			else {
				Log("%s: SHA-256 %s, expected %s; downloading it again",
					package->file.String(), sha.String(),
					package->sha256.String());
				unlink(path);
			}
		}
		if (!verified) {
			_error.SetToFormat("%s does not have the checksum the release "
				"names", package->file.String());
			return B_BAD_DATA;
		}

		error = _VerifyPackage(path, *expected, _error);
		if (error != B_OK) {
			unlink(path);
			return error;
		}
		progress.done += package->size;
	}

	// the Raspberry Pi's loader and boot archive for the FAT partition
	plan.bootSources.MakeEmpty();
	if (plan.goBackState.IsEmpty() && PiBoot::IsPi()) {
		if (plan.release.BootFiles().IsEmpty()) {
			_error = "the release has no boot files for the Raspberry Pi";
			return B_BAD_DATA;
		}
		for (int32 i = 0; const BootFile* file
				= plan.release.BootFiles().ItemAt(i); i++) {
			BString path(kDownloadsDirectory);
			path << '/' << file->sha256 << '-' << file->name;
			_Status("Downloading %s…", file->name.String());
			error = fFetcher.FetchFile(file->url, path, file->size, _error,
				NULL);
			BString sha;
			if (error == B_OK)
				error = ComputeSha256(path, sha);
			if (error == B_OK && sha != file->sha256) {
				unlink(path);
				_error.SetToFormat("%s does not have the checksum the release "
					"names", file->name.String());
				error = B_BAD_DATA;
			}
			if (error != B_OK)
				return error;
			plan.bootSources.Add(path);
		}
	}

	_Progress(-1);
	return B_OK;
}


status_t
Updater::Apply(UpdatePlan& plan, BString& _error)
{
	SystemState state;
	status_t error = state.Load();
	if (error == B_OK)
		error = CheckPreconditions(state, _error);
	if (error != B_OK)
		return error;

	if (plan.IsEmpty()) {
		_error = "nothing to change";
		return B_OK;
	}

	BDaemonClient client;
	BActivationTransaction transaction;
	BDirectory transactionDirectory;
	error = client.CreateTransaction(B_PACKAGE_INSTALLATION_LOCATION_SYSTEM,
		transaction, transactionDirectory);
	if (error != B_OK) {
		_error.SetToFormat("the package daemon cannot start a transaction: %s",
			strerror(error));
		return error;
	}
	BEntry transactionEntry;
	transactionDirectory.GetEntry(&transactionEntry);
	BPath transactionPath(&transactionEntry);

	// Move the downloads into the transaction directory, copy old states'
	// files. On failure, the downloads go back.
	BStringList moved;
	BStringList movedFrom;
	auto restore = [&]() {
		for (int32 i = 0; i < moved.CountStrings(); i++)
			rename(moved.StringAt(i), movedFrom.StringAt(i));
		_RemoveDirectory(transactionPath.Path());
	};

	off_t copied = 0;
	int32 count = plan.activate.CountItems();
	for (int32 i = 0; i < count; i++) {
		PlannedPackage* package = plan.activate.ItemAt(i);
		BString target(transactionPath.Path());
		target << '/' << package->file;
		if (package->copy) {
			_Status("Copying %s (%" B_PRId32 " of %" B_PRId32 ")…",
				package->file.String(), i + 1, count);
			error = copy_file(package->source, target, fListener, copied,
				plan.copyBytes, &fCancelled);
		} else {
			error = rename(package->source, target) == 0 ? B_OK : errno;
			if (error == B_OK) {
				moved.Add(target);
				movedFrom.Add(package->source);
			}
		}
		if (error != B_OK) {
			_error.SetToFormat("cannot prepare %s: %s", package->file.String(),
				strerror(error));
			restore();
			return error;
		}
		transaction.AddPackageToActivate(package->file);
	}
	for (int32 i = 0; i < plan.deactivate.CountStrings(); i++)
		transaction.AddPackageToDeactivate(plan.deactivate.StringAt(i));
	transaction.SetDeferActivation(true);

	// Raspberry Pi: the release's kernel on the FAT partition, before any
	// package changes (it is not used until selected below)
	if (!plan.bootSources.IsEmpty()) {
		PiBoot boot;
		error = boot.Mount(_error);
		if (error == B_OK) {
			error = boot.Install(plan.release.Version(),
				plan.release.BootFiles(), plan.bootSources, _error);
		}
		if (error != B_OK) {
			restore();
			return error;
		}
	}

	// The trial goes in before the commit: if the commit is cut short, the
	// boot loader returns to the state the commit made ("newest").
	BString releaseId = plan.goBackState.IsEmpty() ? plan.release.Id()
		: BString(plan.goBackState);
	error = SystemState::StartTrial(releaseId, "newest", kTrialAttempts);
	if (error != B_OK) {
		_error.SetToFormat("cannot write the trial file: %s", strerror(error));
		restore();
		return error;
	}

	_Status("Installing…");
	_Progress(-1);
	Log("committing: %" B_PRId32 " packages to activate, %" B_PRId32
		" to deactivate (%s)", count, plan.deactivate.CountStrings(),
		releaseId.String());
	BCommitTransactionResult result;
	error = client.CommitTransaction(transaction, result);
	if (error == B_OK && result.Error() != B_TRANSACTION_OK)
		error = B_ERROR;
	if (error != B_OK) {
		_error = result.FullErrorMessage();
		if (_error.IsEmpty())
			_error = strerror(error);
		Log("commit failed: %s", _error.String());
		SystemState::EndTrial();
		restore();
		return error;
	}

	BString oldState = result.OldStateDirectory();
	Log("committed; the previous version is %s", oldState.String());
	SystemState::StartTrial(releaseId, oldState, kTrialAttempts);
	_RemoveDirectory(transactionPath.Path());

	if (plan.goBackState.IsEmpty()) {
		Release installed, previous;
		bool havePrevious = LoadInstalledRelease(installed) == B_OK
			&& _LoadReleaseRecord(installed.Id(), previous) == B_OK;
		_SaveReleaseRecord(plan.release);
		BString report;
		ApplyPayloadLinks(plan.release, havePrevious ? &previous : NULL,
			report);
	}

	BString report;
	SyncBootFiles(report);
	_Status("Restart to finish.");
	return B_OK;
}


status_t
Updater::_RemoveDirectory(const char* path)
{
	DIR* dir = opendir(path);
	if (dir == NULL)
		return errno;
	while (dirent* entry = readdir(dir)) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		BString child(path);
		child << '/' << entry->d_name;
		struct stat st;
		if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
			_RemoveDirectory(child);
		else
			unlink(child);
	}
	closedir(dir);
	return rmdir(path) == 0 ? B_OK : errno;
}


// #pragma mark - after a start


status_t
Updater::BootCheck(BString& _report, bool waitForDesktop)
{
	SystemState state;
	status_t error = state.Load();
	if (error != B_OK)
		return error;

	TrialInfo trial;
	SystemState::ReadTrial(trial);
	LastBoot lastBoot = SystemState::ReadLastBoot();

	// a request to start an old state once has been used
	unlink(kStateDirectory "/" AIROS_BOOT_NEXT_DONE_FILE);

	if (!state.RunningLatest()) {
		if (lastBoot.mode == AIROS_BOOT_MODE_FALLBACK) {
			_report = "The update did not start; the previous version runs.";
			Log("running %s after the update %s did not start (%" B_PRId32
				" starts)", state.RunningState().String(),
				trial.release.String(), trial.attempts);
		} else {
			_report.SetToFormat("Running the older version %s.",
				state.RunningState().String());
		}
		BString report;
		SyncBootFiles(report);
		return B_OK;
	}

	if (trial.exists) {
		if (waitForDesktop && !_WaitForDesktop()) {
			_report = "The desktop did not come up and stay; the update is not "
				"confirmed.";
			Log("%s", _report.String());
			return B_ERROR;
		}
		BString driver;
		if (_DriverStartFailed(driver)) {
			// The driver keeps itself off after a start that brought the
			// machine down, and the desktop then comes up on the frame
			// buffer driver, often showing nothing: not a start to confirm.
			// The next start falls back, and the kernel lets the driver try
			// again with the previous version (vfs_boot.cpp).
			_report.SetToFormat("%s did not start with the update (it brought "
				"an earlier start down); the update is not confirmed.",
				driver.String());
			Log("%s", _report.String());
			return B_ERROR;
		}
		error = Confirm(_report);
		if (error != B_OK)
			return error;
	}

	// a system installed from an image: remember its release's packages
	Release installed;
	if (LoadInstalledRelease(installed) == B_OK) {
		Release record;
		if (_LoadReleaseRecord(installed.Id(), record) != B_OK) {
			BString text("{\"format\": 1, \"bootstrap\": true, \"id\": ");
			text << json::Quote(installed.Id()) << ", \"target\": "
				<< json::Quote(installed.Target()) << ", \"version\": "
				<< json::Quote(installed.Version()) << ", \"packages\": [";
			const InstalledVersion* latest = state.Latest();
			for (int32 i = 0; i < latest->packages.CountStrings(); i++) {
				const BString& file = latest->packages.StringAt(i);
				text << (i > 0 ? ", " : "") << "{\"name\": "
					<< json::Quote(PackageNameOf(file)) << ", \"file\": "
					<< json::Quote(file) << "}";
			}
			text << "], \"payload\": {\"links\": [";
			int32 count = 0;
			_AddRunningPayloadLinks(kPayloadDirectory, "", text, count);
			text << "]}}\n";
			SystemState::EnsureDirectory(kReleasesDirectory);
			BString path(kReleasesDirectory);
			path << '/' << installed.Id() << ".json";
			SystemState::WriteFileAtomically(path, text);
			Log("recorded the packages of %s, installed from an image",
				installed.Id().String());
		}
	}

	BString report;
	SyncBootFiles(report);
	return B_OK;
}


/*!	Whether the desktop is up: Tracker and Deskbar running, the same teams
	for kConfirmDelay without a break. Gives up after ten minutes.
*/
/*!	Whether a kernel driver's start marker (\c <driver>.starting in the
	drivers' settings, see amdgpu_drm) was left by a start of the update on
	trial: made after the update was installed, and before this boot (a
	driver starting right now has a marker of its own, which it deletes once
	it runs stably).
*/
bool
Updater::_DriverStartFailed(BString& _driver)
{
	struct stat trialStat;
	if (stat(kStateDirectory "/" AIROS_BOOT_TRIAL_FILE, &trialStat) != 0)
		return false;
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append("kernel/drivers") != B_OK) {
		return false;
	}
	DIR* dir = opendir(path.Path());
	if (dir == NULL)
		return false;

	const time_t bootTime = real_time_clock() - system_time() / 1000000;
	static const char kSuffix[] = ".starting";
	const size_t suffixLength = sizeof(kSuffix) - 1;
	bool failed = false;
	while (dirent* entry = readdir(dir)) {
		size_t length = strlen(entry->d_name);
		if (length <= suffixLength
			|| strcmp(entry->d_name + length - suffixLength, kSuffix) != 0) {
			continue;
		}
		BString file(path.Path());
		file << '/' << entry->d_name;
		struct stat st;
		if (stat(file, &st) != 0 || st.st_mtime < trialStat.st_mtime
			|| st.st_mtime + 5 >= bootTime) {
			continue;
		}
		_driver.SetTo(entry->d_name, length - suffixLength);
		failed = true;
		break;
	}
	closedir(dir);
	return failed;
}


bool
Updater::_WaitForDesktop()
{
	const bigtime_t deadline = system_time() + 600000000;
	team_id tracker = -1, deskbar = -1;
	bigtime_t since = -1;
	while (system_time() < deadline) {
		team_id currentTracker = be_roster->TeamFor("application/x-vnd.Be-TRAK");
		team_id currentDeskbar = be_roster->TeamFor("application/x-vnd.Be-TSKB");
		if (currentTracker < 0 || currentDeskbar < 0
			|| currentTracker != tracker || currentDeskbar != deskbar) {
			// not up yet, or started again: the half minute starts over
			tracker = currentTracker;
			deskbar = currentDeskbar;
			since = currentTracker >= 0 && currentDeskbar >= 0
				? system_time() : -1;
		} else if (since >= 0 && system_time() - since >= kConfirmDelay)
			return true;
		snooze(1000000);
	}
	return false;
}


status_t
Updater::Confirm(BString& _report)
{
	TrialInfo trial;
	if (SystemState::ReadTrial(trial) != B_OK) {
		_report = "No update waits for confirmation.";
		return B_OK;
	}
	SystemState::EndTrial();
	Log("confirmed %s after %" B_PRId32 " start(s)", trial.release.String(),
		trial.attempts);
	_report.SetToFormat("The update %s works.", trial.release.String());

	// What a confirmed release brings along: its EFI loader, unless this was
	// a return to an older version (a trial named after its state). A newer
	// loader starts older states; an older one may not know newer ones.
	BString report;
	if (!trial.release.StartsWith("state_")) {
		UpdateEfiLoader(report);
		if (!report.IsEmpty())
			_report << "\n" << report;
	}

	Settings settings;
	settings.Load();
	report.Truncate(0);
	Prune(settings.versionsToKeep, report);
	if (!report.IsEmpty())
		_report << "\n" << report;

	// downloads that went in are gone; drop what is left over
	_RemoveDirectory(kDownloadsDirectory);
	return B_OK;
}


status_t
Updater::RemoveFailedUpdate(BString& _error)
{
	// Running the fallback after an update that did not start: make the
	// running version the current one again.
	SystemState state;
	status_t error = state.Load();
	if (error != B_OK)
		return error;
	if (state.RunningLatest()) {
		_error = "the current version runs; there is nothing to remove";
		return B_BAD_VALUE;
	}

	const InstalledVersion* running = state.Running();
	UpdatePlan plan;
	error = PlanGoBack(*running, state, plan, _error);
	// The update on trial did not start when this start fell back from it
	// (and it was installed: a transaction cut short leaves no release that
	// failed).
	TrialInfo failedTrial;
	SystemState::ReadTrial(failedTrial);
	LastBoot lastBoot = SystemState::ReadLastBoot();
	BString failedVersion = Release::VersionForId(failedTrial.release);
	bool lastBootWasFallback = failedTrial.exists
		&& lastBoot.mode == AIROS_BOOT_MODE_FALLBACK
		&& state.Latest()->bootable
		&& state.Latest()->releaseVersion == failedVersion;
	if (error != B_OK)
		return error;

	// The trial must not block this transaction.
	SystemState::EndTrial();

	BDaemonClient client;
	BActivationTransaction transaction;
	BDirectory transactionDirectory;
	error = client.CreateTransaction(B_PACKAGE_INSTALLATION_LOCATION_SYSTEM,
		transaction, transactionDirectory);
	if (error != B_OK) {
		_error.SetToFormat("the package daemon cannot start a transaction: %s",
			strerror(error));
		return error;
	}
	BEntry transactionEntry;
	transactionDirectory.GetEntry(&transactionEntry);
	BPath transactionPath(&transactionEntry);

	off_t copied = 0;
	for (int32 i = 0; PlannedPackage* package = plan.activate.ItemAt(i); i++) {
		BString target(transactionPath.Path());
		target << '/' << package->file;
		error = copy_file(package->source, target, fListener, copied,
			plan.copyBytes, &fCancelled);
		if (error != B_OK) {
			_error.SetToFormat("cannot copy %s: %s", package->file.String(),
				strerror(error));
			_RemoveDirectory(transactionPath.Path());
			return error;
		}
		transaction.AddPackageToActivate(package->file);
	}
	for (int32 i = 0; i < plan.deactivate.CountStrings(); i++)
		transaction.AddPackageToDeactivate(plan.deactivate.StringAt(i));
	transaction.SetDeferActivation(true);

	BCommitTransactionResult result;
	error = client.CommitTransaction(transaction, result);
	if (error == B_OK && result.Error() != B_TRANSACTION_OK)
		error = B_ERROR;
	_RemoveDirectory(transactionPath.Path());
	if (error != B_OK) {
		_error = result.FullErrorMessage();
		Log("removing the failed update failed: %s", _error.String());
		return error;
	}
	Log("removed the update that did not start; it is kept in %s",
		result.OldStateDirectory().String());
	if (lastBootWasFallback)
		SystemState::AddFailedRelease(failedVersion);
	BString report;
	SyncBootFiles(report);
	return B_OK;
}


status_t
Updater::Prune(int32 keep, BString& _report)
{
	SystemState state;
	status_t error = state.Load();
	if (error != B_OK)
		return error;
	if (!state.RunningLatest() || state.RestartPending())
		return B_OK;

	TrialInfo trial;
	SystemState::ReadTrial(trial);

	// Walk the old states from the newest; once more than "keep" releases
	// older than the current one have been seen, every older state goes.
	const InstalledVersion* latest = state.Latest();
	BStringList releases;
	int32 cut = -1;
	for (int32 i = 1; const InstalledVersion* version = state.VersionAt(i);
			i++) {
		if (version->SameRelease(*latest)
			|| SystemState::IsFailedRelease(version->releaseVersion)) {
			continue;
		}
		BString key = !version->releaseVersion.IsEmpty()
			? version->releaseVersion : version->systemPackage;
		if (!releases.HasString(key)) {
			if (releases.CountStrings() == keep) {
				cut = i;
				break;
			}
			releases.Add(key);
		}
	}
	if (cut < 0)
		return B_OK;

	// A state needs the newer state directories, so only the oldest go.
	int32 removed = 0;
	for (int32 i = state.CountVersions() - 1; i >= cut; i--) {
		const InstalledVersion* version = state.VersionAt(i);
		if (version->state == trial.fallback)
			break;
		BString path(kAdminDirectory);
		path << '/' << version->state;
		if (_RemoveDirectory(path) == B_OK) {
			Log("removed the old state %s (%s)", version->state.String(),
				version->Title().String());
			removed++;
		}
	}
	if (removed > 0) {
		_report.SetToFormat("Removed %" B_PRId32 " old version state(s).",
			removed);
	}

	if (PiBoot::IsPi()) {
		SystemState remaining;
		BStringList keep;
		if (remaining.Load() == B_OK) {
			for (int32 i = 0; const InstalledVersion* version
					= remaining.VersionAt(i); i++) {
				keep.Add(version->releaseVersion);
			}
			PiBoot boot;
			BString error;
			if (boot.Mount(error) == B_OK)
				boot.Prune(keep);
		}
	}
	return B_OK;
}


// #pragma mark - what starts next


/*!	The state the boot loader starts next, as it decides
	(airos_choose_boot_state()): empty for the latest one.
*/
BString
Updater::NextBootState(const SystemState& state) const
{
	struct Reader {
		const SystemState& state;

		ssize_t ReadFile(const char* name, char* buffer, size_t size)
		{
			BString path(kStateDirectory);
			path << '/' << name;
			BFile file(path, B_READ_ONLY);
			if (file.InitCheck() != B_OK)
				return file.InitCheck();
			ssize_t length = file.Read(buffer, size);
			if (length >= 0 && (size_t)length < size)
				buffer[length] = '\0';
			return length;
		}

		bool StateExists(const char* name)
		{
			const InstalledVersion* version = state.FindState(name);
			return version != NULL && !version->latest && version->bootable;
		}

		bool LatestBootable()
		{
			return state.Latest() != NULL && state.Latest()->bootable;
		}

		int LoaderTrialStarts(const char* key)
		{
			// (an EFI variable applications can't read)
			return 0;
		}

		const char* NewestOldState()
		{
			const InstalledVersion* version = state.NewestOldState();
			return version != NULL ? version->state.String() : NULL;
		}
	} reader = { state };

	char name[B_FILE_NAME_LENGTH];
	airos_choose_boot_state(reader, name, sizeof(name));
	return name;
}


/*!	On the Raspberry Pi the kernel comes from the FAT partition, not from the
	package state: name the loader and boot archive of the version that
	starts next in airos-boot.txt. Elsewhere there is nothing to do.
*/
status_t
Updater::SyncBootFiles(BString& _report)
{
	if (!PiBoot::IsPi())
		return B_OK;

	SystemState state;
	status_t error = state.Load();
	if (error != B_OK)
		return error;
	BString next = NextBootState(state);
	const InstalledVersion* version = state.FindState(next);
	if (version == NULL)
		return B_ENTRY_NOT_FOUND;

	PiBoot boot;
	BString message;
	error = boot.Mount(message);
	if (error != B_OK) {
		Log("cannot reach the boot partition: %s", message.String());
		return error;
	}
	// a version installed by an update has its own pair, the image's own
	// version the one at the top
	const char* wanted = boot.HasVersion(version->releaseVersion)
		? version->releaseVersion.String() : NULL;
	error = boot.Select(wanted, message);
	if (error != B_OK)
		Log("cannot select the boot files: %s", message.String());
	return error;
}


// #pragma mark - EFI loader


namespace {

struct EspFinder : BDiskDeviceVisitor {
	partition_id	id;

	EspFinder() : id(-1) {}

	virtual bool Visit(BDiskDevice*)
	{
		return false;
	}

	virtual bool Visit(BPartition* partition, int32)
	{
		if (partition->Type() != NULL
			&& strcmp(partition->Type(), "EFI system data") == 0
			&& partition->ContentType() != NULL
			&& strcmp(partition->ContentType(), kPartitionTypeFAT32) == 0) {
			id = partition->ID();
			return true;
		}
		return false;
	}
};

}	// namespace


status_t
Updater::UpdateEfiLoader(BString& _report)
{
	if (!file_exists(kEfiLoaderFile))
		return B_OK;

	// the EFI system partition of the disk the system runs from
	BVolume bootVolume;
	BVolumeRoster().GetBootVolume(&bootVolume);
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* bootPartition;
	if (roster.FindPartitionByVolume(bootVolume, &device, &bootPartition)
			!= B_OK) {
		return B_OK;
	}
	EspFinder finder;
	device.VisitEachDescendant(&finder);
	if (finder.id < 0)
		return B_OK;
	BPartition* esp = device.FindDescendant(finder.id);
	if (esp == NULL || esp->IsReadOnly())
		return B_OK;

	bool mounted = false;
	if (!esp->IsMounted()) {
		dev_t volume = esp->Mount();
		if (volume < 0) {
			Log("cannot mount the EFI system partition: %s", strerror(volume));
			return volume;
		}
		mounted = true;
	}
	BPath mountPoint;
	status_t error = esp->GetMountPoint(&mountPoint);

#if defined(__x86_64__)
	const char* name = "BOOTX64.EFI";
#elif defined(__aarch64__)
	const char* name = "BOOTAA64.EFI";
#else
	const char* name = NULL;
#endif

	BString loader;
	if (error == B_OK && name != NULL) {
		loader.SetToFormat("%s/EFI/BOOT/%s", mountPoint.Path(), name);
		BString current, wanted;
		if (ComputeSha256(kEfiLoaderFile, wanted) == B_OK
			&& (!file_exists(loader) || (ComputeSha256(loader, current) == B_OK
				&& current != wanted))) {
			BString directory;
			directory.SetToFormat("%s/EFI/BOOT", mountPoint.Path());
			create_directory(directory, 0755);
			BString temporary(loader), previous(loader);
			temporary << ".new";
			previous.Truncate(previous.Length() - 4);
			previous << "-previous.EFI";
			off_t done = 0;
			error = copy_file(kEfiLoaderFile, temporary, NULL, done, 0, NULL);
			BString copiedSha;
			if (error == B_OK)
				error = ComputeSha256(temporary, copiedSha);
			if (error == B_OK && copiedSha != wanted)
				error = B_IO_ERROR;
			if (error == B_OK) {
				// FAT's read-only flag would make the write fail half-way
				chmod(loader, 0644);
				unlink(previous);
				if (file_exists(loader))
					rename(loader, previous);
				if (rename(temporary, loader) != 0) {
					error = errno;
					rename(previous, loader);
				}
			}
			sync();
			if (error == B_OK) {
				Log("updated the EFI loader %s (previous one kept as %s)",
					loader.String(), previous.String());
				_report.SetToFormat("Updated the boot loader.");
			} else {
				unlink(temporary);
				Log("cannot update the EFI loader %s: %s", loader.String(),
					strerror(error));
			}
		}
	}

	if (mounted)
		esp->Unmount();
	return error;
}


// #pragma mark - payload links


/*!	The links of the running airos_payload package, as a JSON list, for the
	record of a release installed from an image.
*/
void
Updater::_AddRunningPayloadLinks(const char* path, const char* relative,
	BString& _json, int32& _count)
{
	DIR* dir = opendir(path);
	if (dir == NULL)
		return;
	while (dirent* entry = readdir(dir)) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		BString child(path), childRelative(relative);
		child << '/' << entry->d_name;
		if (!childRelative.IsEmpty())
			childRelative << '/';
		childRelative << entry->d_name;

		struct stat st;
		if (lstat(child, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			_AddRunningPayloadLinks(child, childRelative, _json, _count);
			continue;
		}
		_json << (_count++ > 0 ? ", " : "") << "{\"path\": "
			<< json::Quote(childRelative);
		if (S_ISLNK(st.st_mode)) {
			char target[B_PATH_NAME_LENGTH];
			ssize_t length = readlink(child, target, sizeof(target) - 1);
			target[length > 0 ? length : 0] = '\0';
			_json << ", \"type\": \"symlink\", \"target\": "
				<< json::Quote(target);
		} else
			_json << ", \"type\": \"file\"";
		_json << "}";
	}
	closedir(dir);
}


/*!	Creates the system/non-packaged links of the files \a release adds to the
	payload. Links that the previous release had are left as they are: the
	system may have removed or renamed one on purpose (the Dragon's boot script
	sets the Panfrost EGL vendor aside), and a real file in a link's place is
	the user's own build, which wins.
	A new kernel driver's link dangles until the restart (the running payload
	does not have the file), so the kernel's driver watcher cannot load it
	while the system runs.
*/
status_t
Updater::ApplyPayloadLinks(const Release& release, const Release* previous,
	BString& _report)
{
	BStringList known;
	if (previous != NULL) {
		for (int32 i = 0; const PayloadLink* link
				= previous->PayloadLinks().ItemAt(i); i++) {
			known.Add(link->path);
		}
	} else {
		// without a record of the previous release, only what is missing
		// from the running payload too is new
		for (int32 i = 0; const PayloadLink* link
				= release.PayloadLinks().ItemAt(i); i++) {
			BString running(kPayloadDirectory);
			running << '/' << link->path;
			if (file_exists(running))
				known.Add(link->path);
		}
	}

	int32 created = 0;
	int32 kept = 0;
	for (int32 i = 0; const PayloadLink* link = release.PayloadLinks().ItemAt(i);
			i++) {
		if (known.HasString(link->path))
			continue;

		BString path(kNonPackagedDirectory);
		path << '/' << link->path;
		BString target;
		if (link->type == "symlink")
			target = link->target;
		else
			target.SetToFormat("%s/%s", kPayloadDirectory, link->path.String());

		struct stat st;
		if (lstat(path, &st) == 0) {
			if (!S_ISLNK(st.st_mode))
				kept++;
			continue;
		}

		// never make a driver appear that the running system has
		BString running(kPayloadDirectory);
		running << '/' << link->path;
		if (link->path.StartsWith("add-ons/kernel/") && file_exists(running)
			&& link->type != "symlink") {
			continue;
		}

		BPath parent(path);
		parent.GetParent(&parent);
		create_directory(parent.Path(), 0755);
		if (symlink(target, path) == 0)
			created++;
		else
			Log("cannot link %s: %s", path.String(), strerror(errno));
	}

	if (created > 0 || kept > 0) {
		Log("payload links: %" B_PRId32 " created, %" B_PRId32 " real files "
			"kept", created, kept);
		_report.SetToFormat("%" B_PRId32 " new system files linked.", created);
	}
	return B_OK;
}
