/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "SystemState.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <package/InstallationLocationInfo.h>
#include <package/PackageInfoSet.h>

#include <airos_boot_control.h>
#include <package/DaemonClient.h>

#include "Constants.h"
#include "Release.h"
#include <stdlib.h>


using namespace BPackageKit;
using BPackageKit::BPrivate::BDaemonClient;


static BString
boot_control_path(const char* name)
{
	BString path(kStateDirectory);
	path << '/' << name;
	return path;
}


static status_t
read_text_file(const char* path, BString& _text)
{
	BFile file(path, B_READ_ONLY);
	status_t error = file.InitCheck();
	if (error != B_OK)
		return error;
	off_t size;
	if (file.GetSize(&size) != B_OK || size > 1024 * 1024)
		return B_BAD_DATA;
	char* buffer = _text.LockBuffer(size + 1);
	ssize_t bytesRead = file.ReadAt(0, buffer, size);
	_text.UnlockBuffer(bytesRead > 0 ? bytesRead : 0);
	return bytesRead < 0 ? bytesRead : B_OK;
}


static bool
is_system_package_file(const char* name)
{
	return strcmp(name, "haiku.hpkg") == 0 || strncmp(name, "haiku-", 6) == 0;
}


// #pragma mark - InstalledVersion


BString
InstalledVersion::Title() const
{
	if (!releaseVersion.IsEmpty())
		return Release::TitleForVersion(releaseVersion);
	BString title("Haiku");
	BString revision = Revision();
	if (!revision.IsEmpty())
		title << " " << revision;
	return title;
}


BString
InstalledVersion::Revision() const
{
	// "haiku-r1~beta5_hrev60207_1930-1-x86_64.hpkg" -> "hrev60207+1930"
	int32 start = systemPackage.FindFirst("hrev");
	if (start < 0)
		return "";
	BString revision;
	systemPackage.CopyInto(revision, start, systemPackage.Length() - start);
	int32 end = revision.FindFirst('-');
	if (end >= 0)
		revision.Truncate(end);
	revision.ReplaceAll('_', '+');
	return revision;
}


BString
InstalledVersion::StateTitle() const
{
	// "state_2026-10-26_09:20:11"
	static const char* const kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May",
		"Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	int year, month, day, hour, minute, second;
	if (sscanf(state.String(), "state_%d-%d-%d_%d:%d:%d", &year, &month, &day,
			&hour, &minute, &second) == 6 && month >= 1 && month <= 12) {
		BString title;
		title.SetToFormat("%d %s %d %02d:%02d:%02d", day, kMonths[month - 1],
			year, hour, minute, second);
		return title;
	}
	return state;
}


bool
InstalledVersion::SameRelease(const InstalledVersion& other) const
{
	if (!releaseVersion.IsEmpty() || !other.releaseVersion.IsEmpty())
		return releaseVersion == other.releaseVersion;
	return systemPackage == other.systemPackage;
}


// #pragma mark - SystemState


SystemState::SystemState()
	:
	fVersions(10),
	fRestartPending(false),
	fChangeCount(-1)
{
}


status_t
SystemState::Load()
{
	fVersions.MakeEmpty();
	fRunningState.Truncate(0);
	fRestartPending = false;
	fChangeCount = -1;

	// the latest state
	status_t error = _AddVersion(NULL, kAdminDirectory "/" kActivationFile);
	if (error != B_OK)
		return error;

	// the old states, newest first
	BStringList states;
	if (DIR* dir = opendir(kAdminDirectory)) {
		while (dirent* entry = readdir(dir)) {
			if (strncmp(entry->d_name, "state_", 6) == 0)
				states.Add(entry->d_name);
		}
		closedir(dir);
	}
	states.Sort();
	for (int32 i = states.CountStrings() - 1; i >= 0; i--) {
		BString path(kAdminDirectory);
		path << '/' << states.StringAt(i) << "/" kActivationFile;
		_AddVersion(states.StringAt(i), path);
	}

	// which one runs
	BDaemonClient client;
	BInstallationLocationInfo info;
	if (client.GetInstallationLocationInfo(
			B_PACKAGE_INSTALLATION_LOCATION_SYSTEM, info) == B_OK) {
		fRunningState = info.ActiveStateName();
		fChangeCount = info.ChangeCount();
		fRestartPending = fRunningState.IsEmpty()
			&& info.CurrentlyActivePackageInfos().CountInfos() > 0;
	}

	for (int32 i = 0; InstalledVersion* version = fVersions.ItemAt(i); i++)
		version->running = version->state == fRunningState;

	// whether each state still has its system package
	for (int32 i = 0; InstalledVersion* version = fVersions.ItemAt(i); i++) {
		BPath path;
		version->bootable = !version->systemPackage.IsEmpty()
			&& FindPackageFile(*version, version->systemPackage, path) == B_OK;
	}

	return B_OK;
}


const InstalledVersion*
SystemState::Latest() const
{
	return fVersions.ItemAt(0);
}


const InstalledVersion*
SystemState::Running() const
{
	for (int32 i = 0; InstalledVersion* version = fVersions.ItemAt(i); i++) {
		if (version->running)
			return version;
	}
	return Latest();
}


const InstalledVersion*
SystemState::FindState(const char* state) const
{
	if (state == NULL || state[0] == '\0' || strcmp(state, "latest") == 0)
		return Latest();
	for (int32 i = 1; InstalledVersion* version = fVersions.ItemAt(i); i++) {
		if (version->state == state)
			return version;
	}
	return NULL;
}


const InstalledVersion*
SystemState::NewestOldState() const
{
	for (int32 i = 1; InstalledVersion* version = fVersions.ItemAt(i); i++) {
		if (version->bootable)
			return version;
	}
	return NULL;
}


status_t
SystemState::FindPackageFile(const InstalledVersion& version,
	const char* fileName, BPath& _path) const
{
	// A state's packages are in its own directory or in the newer ones,
	// up to the packages directory itself.
	int32 index = fVersions.IndexOf(&version);
	if (index < 0)
		return B_BAD_VALUE;

	for (int32 i = index; i >= 0; i--) {
		const InstalledVersion* other = fVersions.ItemAt(i);
		BString path;
		if (other->latest)
			path.SetToFormat("%s/%s", kPackagesDirectory, fileName);
		else {
			path.SetToFormat("%s/%s/%s", kAdminDirectory,
				other->state.String(), fileName);
		}
		struct stat st;
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
			return _path.SetTo(path);
	}
	return B_ENTRY_NOT_FOUND;
}


/*static*/ status_t
SystemState::ReadActivationFile(const char* path, BStringList& _packages)
{
	_packages.MakeEmpty();
	BString text;
	status_t error = read_text_file(path, text);
	if (error != B_OK)
		return error;
	text.Split("\n", true, _packages);
	for (int32 i = _packages.CountStrings() - 1; i >= 0; i--) {
		BString line = _packages.StringAt(i);
		line.Trim();
		if (line.IsEmpty())
			_packages.Remove(i);
		else
			_packages.Replace(i, line);
	}
	return B_OK;
}


status_t
SystemState::_AddVersion(const char* state, const char* activationFile)
{
	InstalledVersion* version = new InstalledVersion;
	version->state = state;
	version->latest = state == NULL;
	version->running = false;
	version->bootable = false;

	if (ReadActivationFile(activationFile, version->packages) != B_OK) {
		if (state != NULL) {
			delete version;
			return B_ENTRY_NOT_FOUND;
		}
		// no activation file (yet): every package in the directory
		if (DIR* dir = opendir(kPackagesDirectory)) {
			while (dirent* entry = readdir(dir)) {
				BString name(entry->d_name);
				if (name.EndsWith(".hpkg"))
					version->packages.Add(name);
			}
			closedir(dir);
		}
	}

	for (int32 i = 0; i < version->packages.CountStrings(); i++) {
		const BString& name = version->packages.StringAt(i);
		if (version->systemPackage.IsEmpty() && is_system_package_file(name))
			version->systemPackage = name;
		else if (name.StartsWith(kReleasePackageName "-")) {
			version->releaseFile = name;
			// "airos_release-20261026.0915-1-x86_64.hpkg"
			int32 start = name.FindFirst('-') + 1;
			int32 end = name.FindFirst('-', start);
			if (end > start)
				name.CopyInto(version->releaseVersion, start, end - start);
		}
	}

	fVersions.AddItem(version);
	return B_OK;
}


/*!	A word that names one trial start: the boot loader counts its starts of
	the trial under it.
*/
static BString
new_trial_token()
{
	BString token;
	token.SetToFormat("%" B_PRIx64 "-%" B_PRIx32, (uint64)real_time_clock_usecs(),
		(uint32)find_thread(NULL));
	return token;
}


// #pragma mark - boot control


/*static*/ status_t
SystemState::ReadTrial(TrialInfo& _trial)
{
	_trial.exists = false;
	_trial.allowed = AIROS_BOOT_DEFAULT_ATTEMPTS;
	_trial.attempts = 0;
	_trial.release.Truncate(0);
	_trial.fallback.Truncate(0);

	BString text;
	if (read_text_file(boot_control_path(AIROS_BOOT_TRIAL_FILE), text) != B_OK)
		return B_ENTRY_NOT_FOUND;
	_trial.exists = true;

	char value[B_FILE_NAME_LENGTH];
	if (airos_boot_find_value(text, text.Length(), "release", value,
			sizeof(value))) {
		_trial.release = value;
	}
	if (airos_boot_find_value(text, text.Length(), "fallback", value,
			sizeof(value))) {
		_trial.fallback = value;
	}
	if (airos_boot_find_value(text, text.Length(), "attempts", value,
			sizeof(value))) {
		_trial.allowed = atoi(value);
		if (_trial.allowed < 1)
			_trial.allowed = 1;
	}

	BString attempts;
	if (read_text_file(boot_control_path(AIROS_BOOT_TRIAL_ATTEMPTS_FILE),
			attempts) == B_OK) {
		for (int32 i = 0; i < attempts.Length(); i++) {
			if (attempts[i] == '\n')
				_trial.attempts++;
		}
	}
	return B_OK;
}


/*static*/ status_t
SystemState::StartTrial(const char* release, const char* fallback,
	int32 allowed)
{
	status_t error = EnsureDirectory(kStateDirectory);
	if (error != B_OK)
		return error;

	unlink(boot_control_path(AIROS_BOOT_TRIAL_ATTEMPTS_FILE));
	BString content;
	content.SetToFormat("release %s\nfallback %s\nattempts %" B_PRId32
		"\ntoken %s\n", release,
		fallback != NULL && fallback[0] != '\0' ? fallback : "newest",
		allowed, new_trial_token().String());
	return WriteFileAtomically(boot_control_path(AIROS_BOOT_TRIAL_FILE),
		content);
}


/*static*/ status_t
SystemState::ResetTrialAttempts()
{
	if (unlink(boot_control_path(AIROS_BOOT_TRIAL_ATTEMPTS_FILE)) != 0
		&& errno != ENOENT) {
		return errno;
	}

	// a new token: the boot loader's own count starts over too
	BString text;
	if (read_text_file(boot_control_path(AIROS_BOOT_TRIAL_FILE), text) == B_OK) {
		BString content;
		int32 start = 0;
		while (start < text.Length()) {
			int32 end = text.FindFirst('\n', start);
			if (end < 0)
				end = text.Length();
			BString line;
			text.CopyInto(line, start, end - start);
			if (!line.StartsWith("token ") && !line.IsEmpty())
				content << line << '\n';
			start = end + 1;
		}
		content << "token " << new_trial_token() << '\n';
		status_t error = WriteFileAtomically(
			boot_control_path(AIROS_BOOT_TRIAL_FILE), content);
		if (error != B_OK)
			return error;
	}
	sync();
	return B_OK;
}


/*static*/ status_t
SystemState::EndTrial()
{
	unlink(boot_control_path(AIROS_BOOT_TRIAL_FILE));
	unlink(boot_control_path(AIROS_BOOT_TRIAL_ATTEMPTS_FILE));
	sync();
	return B_OK;
}


/*static*/ status_t
SystemState::RequestNextBoot(const char* state)
{
	status_t error = EnsureDirectory(kStateDirectory);
	if (error != B_OK)
		return error;
	BString content(state != NULL && state[0] != '\0' ? state : "latest");
	content << '\n';
	return WriteFileAtomically(boot_control_path(AIROS_BOOT_NEXT_FILE),
		content);
}


/*static*/ status_t
SystemState::CancelNextBoot()
{
	if (unlink(boot_control_path(AIROS_BOOT_NEXT_FILE)) != 0
		&& errno != ENOENT) {
		return errno;
	}
	sync();
	return B_OK;
}


/*static*/ BString
SystemState::NextBoot()
{
	BString text;
	if (read_text_file(boot_control_path(AIROS_BOOT_NEXT_FILE), text) != B_OK)
		return "";
	text.Trim();
	return text;
}


/*static*/ bool
SystemState::HasNextBoot()
{
	struct stat st;
	return stat(boot_control_path(AIROS_BOOT_NEXT_FILE), &st) == 0;
}


/*static*/ LastBoot
SystemState::ReadLastBoot()
{
	LastBoot lastBoot;
	BString text;
	if (read_text_file(boot_control_path(AIROS_BOOT_LAST_FILE), text) != B_OK)
		return lastBoot;
	char word[B_FILE_NAME_LENGTH];
	const char* rest = airos_boot_copy_word(text, text.String() + text.Length(),
		word, sizeof(word));
	lastBoot.mode = word;
	airos_boot_copy_word(rest, text.String() + text.Length(), word,
		sizeof(word));
	lastBoot.state = word;
	return lastBoot;
}


/*static*/ bool
SystemState::IsFailedRelease(const char* version)
{
	if (version == NULL || version[0] == '\0')
		return false;
	BString text;
	if (read_text_file(kFailedFile, text) != B_OK)
		return false;
	BStringList lines;
	text.Split("\n", true, lines);
	return lines.HasString(version);
}


/*!	An update that could not be installed when System Updater had closed the
	applications for it: the computer restarts all the same, and System
	Updater says what went wrong after the restart.
*/
/*static*/ status_t
SystemState::RecordFailedInstall(const char* title, const char* error)
{
	EnsureDirectory(kStateDirectory);
	BString text;
	text << title << "\n" << error << "\n";
	return WriteFileAtomically(kInstallFailedFile, text);
}


/*static*/ bool
SystemState::ReadFailedInstall(BString& _title, BString& _error)
{
	BString text;
	if (read_text_file(kInstallFailedFile, text) != B_OK)
		return false;
	int32 newline = text.FindFirst('\n');
	if (newline < 0)
		newline = text.Length();
	text.CopyInto(_title, 0, newline);
	text.CopyInto(_error, newline + 1, text.Length() - newline - 1);
	_error.Trim();
	return true;
}


/*static*/ void
SystemState::ClearFailedInstall()
{
	unlink(kInstallFailedFile);
}


/*static*/ status_t
SystemState::AddFailedRelease(const char* version)
{
	if (version == NULL || version[0] == '\0' || IsFailedRelease(version))
		return B_OK;
	BString text;
	read_text_file(kFailedFile, text);
	text << version << "\n";
	return WriteFileAtomically(kFailedFile, text);
}


/*static*/ status_t
SystemState::WriteFileAtomically(const char* path, const BString& content)
{
	BString temporary(path);
	temporary << ".new";
	BFile file(temporary, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t error = file.InitCheck();
	if (error != B_OK)
		return error;
	ssize_t written = file.Write(content.String(), content.Length());
	if (written != content.Length())
		return written < 0 ? written : B_IO_ERROR;
	file.Sync();
	file.Unset();
	if (rename(temporary, path) != 0)
		return errno;
	sync();
	return B_OK;
}


/*static*/ status_t
SystemState::EnsureDirectory(const char* path)
{
	return create_directory(path, 0755);
}
