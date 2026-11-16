/*
 * airos_mcp - deploying kernel add-ons without taking the machine down.
 * Copyright 2026 air/OS contributors. MIT license.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <image.h>

#include "Mcp.h"
#include "Process.h"
#include "Tools.h"
#include "Util.h"


namespace {

std::string
BaseDirectory(const std::string& location)
{
	if (location == "home")
		return "/boot/home/config/non-packaged/add-ons";
	return "/boot/system/non-packaged/add-ons";
}


// Where a kind of add-on lives below the add-ons directory.
std::string
KindDirectory(const std::string& kind, std::string& error)
{
	if (kind == "driver")
		return "kernel/drivers/bin";
	if (kind == "accelerant")
		return "accelerants";
	if (kind == "bus_manager")
		return "kernel/bus_managers";
	if (kind == "busses" || StartsWith(kind, "busses/"))
		return "kernel/" + kind;
	if (kind == "file_system")
		return "kernel/file_systems";
	if (kind == "network")
		return "kernel/network";
	if (kind == "generic")
		return "kernel/generic";
	if (kind == "media")
		return "media/plugins";
	if (kind == "translator")
		return "Translators";
	if (kind == "input_server/devices" || kind == "input_server/filters"
			|| kind == "input_server/methods")
		return kind;
	if (StartsWith(kind, "kernel/") || StartsWith(kind, "media/"))
		return kind;
	error = "unknown kind " + kind + " (driver, accelerant, bus_manager, "
		"busses/<bus>, file_system, network, generic, media, translator, "
		"input_server/devices, or a path under add-ons)";
	return std::string();
}


bool
IsElf(const std::string& path, std::string& detail)
{
	std::string head;
	if (!ReadFileToString(path, head, 64)) {
		detail = strerror(errno);
		return false;
	}
	if (head.size() < 20 || head.compare(0, 4, "\177ELF", 4) != 0) {
		detail = "not an ELF file";
		return false;
	}
	unsigned char cls = head[4];
	unsigned machine = (unsigned char)head[18] | ((unsigned char)head[19] << 8);
	detail = Format("ELF%d, machine %u (%s)", cls == 2 ? 64 : 32, machine,
		machine == 62 ? "x86_64" : machine == 183 ? "aarch64" : machine == 243
		? "riscv" : "other");
	return true;
}


bool
SameFile(const std::string& a, const std::string& b)
{
	struct stat sa, sb;
	if (stat(a.c_str(), &sa) != 0 || stat(b.c_str(), &sb) != 0)
		return false;
	return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}


std::string
Sha256(const std::string& path)
{
	RunResult run = RunShell("sha256sum '" + path + "'", 60000000);
	std::string sum = TrimString(run.output);
	size_t space = sum.find(' ');
	return run.exitCode == 0 ? sum.substr(0, space) : std::string();
}


// Kernel images loaded now, by name.
JsonValue
LoadedKernelImages(const std::string& name)
{
	JsonValue out = JsonValue::Array();
	int32 cookie = 0;
	image_info info;
	while (get_next_image_info(B_SYSTEM_TEAM, &cookie, &info) == B_OK) {
		std::string path = info.name;
		std::string base = path;
		size_t slash = base.rfind('/');
		if (slash != std::string::npos)
			base.erase(0, slash + 1);
		if (!name.empty() && strcasestr(base.c_str(), name.c_str()) == NULL
				&& strcasestr(path.c_str(), name.c_str()) == NULL)
			continue;
		JsonValue image = JsonValue::Object();
		image.Set("id", (int64_t)info.id);
		image.Set("name", path);
		image.Set("text", Format("%p", info.text));
		image.Set("text_size", (int64_t)info.text_size);
		out.Push(image);
	}
	return out;
}


// nvidia_rm publishes graphics/nvidia0: a shared prefix of four letters or
// half the driver name counts as a match.
bool
NameMatches(const char* entry, const std::string& name)
{
	if (strcasestr(entry, name.c_str()) != NULL)
		return true;
	size_t common = 0;
	while (common < name.size() && entry[common] != 0
			&& tolower(entry[common]) == tolower(name[common]))
		common++;
	return common >= 4 && common * 2 >= name.size();
}


void
FindDevices(const std::string& dir, const std::string& name, JsonValue& out,
	int depth)
{
	if (depth > 5)
		return;
	DIR* d = opendir(dir.c_str());
	if (d == NULL)
		return;
	while (struct dirent* entry = readdir(d)) {
		if (entry->d_name[0] == '.')
			continue;
		std::string path = dir + "/" + entry->d_name;
		struct stat st;
		if (lstat(path.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (NameMatches(entry->d_name, name)) {
				// a directory named after the driver: list what is in it
				DIR* inner = opendir(path.c_str());
				if (inner != NULL) {
					while (struct dirent* e = readdir(inner)) {
						if (e->d_name[0] != '.')
							out.Push(path + "/" + e->d_name);
					}
					closedir(inner);
				}
			} else
				FindDevices(path, name, out, depth + 1);
		} else if (NameMatches(entry->d_name, name))
			out.Push(path);
	}
	closedir(d);
}


void
ListDevPaths(const JsonValue* paths, JsonValue& status)
{
	if (paths == NULL || !paths->IsArray())
		return;
	JsonValue listed = JsonValue::Array();
	for (size_t i = 0; i < paths->Size(); i++) {
		std::string path = paths->At(i).AsString();
		if (!StartsWith(path, "/dev"))
			path = "/dev/" + path;
		struct stat st;
		if (stat(path.c_str(), &st) != 0) {
			listed.Push(path + ": " + strerror(errno));
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			DIR* d = opendir(path.c_str());
			if (d == NULL)
				continue;
			while (struct dirent* e = readdir(d)) {
				if (e->d_name[0] != '.')
					listed.Push(path + "/" + e->d_name);
			}
			closedir(d);
		} else
			listed.Push(path);
	}
	status.Set("devices", listed);
}


JsonValue
Status(const std::string& name, const std::string& kind, const std::string& location,
	const JsonValue* devices = NULL)
{
	JsonValue status = JsonValue::Object();
	ListDevPaths(devices, status);
	std::string error;
	std::string sub = KindDirectory(kind, error);
	std::string nonPackaged = BaseDirectory(location) + "/" + sub + "/" + name;
	std::string packaged = "/boot/system/add-ons/" + sub + "/" + name;
	JsonValue files = JsonValue::Array();
	const char* candidates[] = { nonPackaged.c_str(), packaged.c_str(), NULL };
	for (int i = 0; candidates[i] != NULL; i++) {
		struct stat st;
		if (stat(candidates[i], &st) != 0)
			continue;
		JsonValue file = JsonValue::Object();
		file.Set("path", candidates[i]);
		file.Set("size", (int64_t)st.st_size);
		file.Set("mtime", (int64_t)st.st_mtime);
		file.Set("sha256", Sha256(candidates[i]));
		files.Push(file);
	}
	status.Set("files", files);
	if (files.Size() > 1)
		status.Set("note", "both a non-packaged and a packaged copy exist; the "
			"non-packaged one wins");
	if (kind == "driver" || StartsWith(kind, "kernel/")) {
		status.Set("loaded_kernel_images", LoadedKernelImages(name));
		JsonValue devices = JsonValue::Array();
		FindDevices("/dev", name, devices, 0);
		status.Set("published_devices", devices);
		std::string devLink = BaseDirectory(location) + "/kernel/drivers/dev";
		JsonValue links = JsonValue::Array();
		FindDevices(devLink, name, links, 0);
		status.Set("dev_links", links);
	}
	return status;
}


ToolResult
DriverStatus(const JsonValue& args)
{
	std::string name = args.GetString("name");
	if (name.empty())
		return ToolResult::Error("name is required");
	std::string kind = args.GetString("kind", "driver");
	std::string location = args.GetString("location", "system");
	std::string error;
	if (KindDirectory(kind, error).empty())
		return ToolResult::Error(error);
	return ToolResult::Json(Status(name, kind, location, args.Get("devices")));
}


ToolResult
DriverDeploy(const JsonValue& args)
{
	std::string source = ExpandPath(args.GetString("source"));
	std::string name = args.GetString("name");
	std::string kind = args.GetString("kind", "driver");
	std::string location = args.GetString("location", "system");
	std::string devLink = args.GetString("dev_link");
	bool swap = args.GetBool("swap", true);
	bool force = args.GetBool("force", false);
	bool keepSource = args.GetBool("keep_source", true);

	if (source.empty())
		return ToolResult::Error("source (a file already on the device, e.g. from "
			"file_put) is required");
	if (name.empty()) {
		size_t slash = source.rfind('/');
		name = slash == std::string::npos ? source : source.substr(slash + 1);
		if (EndsWith(name, ".new"))
			name.erase(name.size() - 4);
	}
	std::string error;
	std::string sub = KindDirectory(kind, error);
	if (sub.empty())
		return ToolResult::Error(error);

	JsonValue result = JsonValue::Object();
	JsonValue steps = JsonValue::Array();
	std::string elfDetail;
	if (!IsElf(source, elfDetail))
		return ToolResult::Error(source + ": " + elfDetail);
	steps.Push("source is " + elfDetail);

	std::string targetDir = BaseDirectory(location) + "/" + sub;
	std::string target = targetDir + "/" + name;
	if (SameFile(source, target))
		return ToolResult::Error("source and target are the same file");

	// Make the directory chain.
	std::string path;
	std::vector<std::string> parts = SplitLines(targetDir);
	for (size_t i = 0; i < targetDir.size(); i++) {
		path += targetDir[i];
		if (targetDir[i] == '/' && path.size() > 1)
			mkdir(path.substr(0, path.size() - 1).c_str(), 0755);
	}
	mkdir(targetDir.c_str(), 0755);

	// Users of the old file first: stopping services and killing teams.
	const JsonValue* services = args.Get("stop_services");
	if (services != NULL && services->IsArray()) {
		for (size_t i = 0; i < services->Size(); i++) {
			std::string service = services->At(i).AsString();
			RunResult run = RunShell("launch_roster stop " + service, 20000000);
			steps.Push(Format("launch_roster stop %s: exit %d %s", service.c_str(),
				run.exitCode, TrimString(run.output).c_str()));
		}
	}
	const JsonValue* teams = args.Get("kill_teams");
	if (teams != NULL && teams->IsArray()) {
		for (size_t i = 0; i < teams->Size(); i++) {
			std::string pattern = teams->At(i).AsString();
			int32 cookie = 0;
			team_info info;
			int killed = 0;
			while (get_next_team_info(&cookie, &info) == B_OK) {
				if (info.team == getpid() || info.team == B_SYSTEM_TEAM)
					continue;
				if (strcasestr(info.args, pattern.c_str()) != NULL) {
					bool gone = KillTeamAndWait(info.team);
					steps.Push(Format("kill %s (team %lld): %s", info.args,
						(long long)info.team, gone ? "gone" : "STILL RUNNING"));
					killed++;
				}
			}
			if (killed == 0)
				steps.Push("kill_teams " + pattern + ": nothing matched");
		}
	}

	// Stage as .new next to the target, so the rename is atomic and on the
	// same volume.
	std::string staged = target + ".new";
	std::string data;
	if (!ReadFileToString(source, data))
		return ToolResult::Error("cannot read " + source);
	struct stat st;
	stat(source.c_str(), &st);
	if (!WriteStringToFile(staged, data, 0755))
		return ToolResult::Error(Format("cannot write %s: %s", staged.c_str(),
			strerror(errno)));
	chmod(staged.c_str(), 0755);
	sync();
	steps.Push(Format("staged %zu bytes as %s and synced", data.size(), staged.c_str()));

	bool existed = access(target.c_str(), F_OK) == 0;
	std::string oldSum = existed ? Sha256(target) : std::string();
	if (existed && (kind == "driver" || StartsWith(kind, "kernel/")) && !force) {
		// The known trap: renaming over a driver whose device a team still
		// has open makes devfs reload it under that team and panics the
		// kernel. Without a way to list open files, the best check is
		// whether the driver is loaded now.
		JsonValue loaded = LoadedKernelImages(name);
		result.Set("loaded_before", loaded);
		if (loaded.Size() > 0 && !args.Has("stop_services") && !args.Has("kill_teams")) {
			result.Set("steps", steps);
			result.Set("staged", staged);
			result.Set("warning", "the driver is loaded in the kernel and no "
				"stop_services/kill_teams were given. Replacing a driver whose device "
				"a team has open reloads it under that team and panics (KDL, and a "
				"reset loses unsynced BFS changes). The new file is staged as .new; "
				"pass stop_services/kill_teams naming the users, or force=true if you "
				"know nothing has it open, or swap=false and reboot to swap by hand.");
			ToolResult out = ToolResult::Json(result);
			out.isError = true;
			return out;
		}
	}

	if (swap) {
		if (rename(staged.c_str(), target.c_str()) != 0)
			return ToolResult::Error(Format("rename to %s failed: %s", target.c_str(),
				strerror(errno)));
		sync();
		steps.Push("renamed into place and synced" + std::string(existed
			? " (replaced the old file)" : " (new file)"));
	} else
		steps.Push("left staged as .new (swap=false)");

	if (!devLink.empty() && kind == "driver") {
		std::string linkPath = BaseDirectory(location) + "/kernel/drivers/dev/" + devLink;
		std::string linkDir = linkPath.substr(0, linkPath.rfind('/'));
		std::string p;
		for (size_t i = 0; i < linkDir.size(); i++) {
			p += linkDir[i];
			if (linkDir[i] == '/' && p.size() > 1)
				mkdir(p.substr(0, p.size() - 1).c_str(), 0755);
		}
		mkdir(linkDir.c_str(), 0755);
		int depth = 0;
		for (size_t i = 0; i < devLink.size(); i++)
			if (devLink[i] == '/')
				depth++;
		std::string relative;
		for (int i = 0; i <= depth; i++)
			relative += "../";
		relative += "bin/" + name;
		unlink(linkPath.c_str());
		if (symlink(relative.c_str(), linkPath.c_str()) == 0)
			steps.Push("dev link " + linkPath + " -> " + relative);
		else
			steps.Push(Format("dev link %s failed: %s", linkPath.c_str(), strerror(errno)));
	}

	if (!keepSource && !SameFile(source, target))
		unlink(source.c_str());

	result.Set("steps", steps);
	result.Set("target", target);
	if (!oldSum.empty())
		result.Set("old_sha256", oldSum);
	result.Set("new_sha256", Sha256(swap ? target : staged));
	if (swap && (kind == "driver" || StartsWith(kind, "kernel/"))) {
		snooze(500000);
		result.Set("status_after", Status(name, kind, location, args.Get("devices")));
		result.Set("note", "devfs reloads a legacy driver when its file changes; the "
			"new code runs on the next open of its device. Services stopped with "
			"launch_roster stop stay disabled until `launch_roster start <name>`. "
			"Check the syslog (syslog_query since the mark) for the driver's init "
			"output.");
	} else if (swap && kind == "accelerant")
		result.Set("note", "app_server loads accelerants at start; a desktop restart "
			"or reboot is needed for the new one to be used");
	return ToolResult::Json(result);
}

} // namespace


void
RegisterDriverTools(McpServer& server)
{
	server.AddTool("driver_deploy",
		"Install a kernel driver or other add-on from a file already on the "
		"device into the non-packaged add-ons tree, the safe way: verify it is "
		"an ELF binary, stop the services and kill the teams named (users of the "
		"old driver), stage it as <name>.new on the same volume, sync, rename "
		"into place, sync, then report what the kernel has loaded and which "
		"/dev entries exist. Refuses to replace a loaded kernel driver when no "
		"users were named, because renaming over a driver whose device is open "
		"panics the kernel (pass force=true to override). Use file_put first to "
		"upload the file.",
		"{\"type\":\"object\",\"properties\":{"
		"\"source\":{\"type\":\"string\",\"description\":\"path of the new binary "
		"on the device\"},"
		"\"name\":{\"type\":\"string\",\"description\":\"installed file name "
		"(default: the source's name)\"},"
		"\"kind\":{\"type\":\"string\",\"description\":\"driver (default), "
		"accelerant, bus_manager, busses/<bus>, file_system, network, generic, "
		"media, translator, input_server/devices, or a path under add-ons\"},"
		"\"location\":{\"type\":\"string\",\"enum\":[\"system\",\"home\"],"
		"\"description\":\"/boot/system/non-packaged (default) or "
		"/boot/home/config/non-packaged\"},"
		"\"dev_link\":{\"type\":\"string\",\"description\":\"for drivers: path under "
		"drivers/dev to symlink to the binary, e.g. graphics/nvidia_rm\"},"
		"\"stop_services\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
		"\"description\":\"launch_roster service names to stop first, e.g. "
		"x-vnd.vos-bluetooth_server\"},"
		"\"kill_teams\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
		"\"description\":\"command line substrings of teams to kill first\"},"
		"\"swap\":{\"type\":\"boolean\",\"description\":\"rename into place "
		"(default true); false leaves <name>.new staged\"},"
		"\"force\":{\"type\":\"boolean\",\"description\":\"replace a loaded driver "
		"even without named users\"},"
		"\"keep_source\":{\"type\":\"boolean\",\"description\":\"default true\"},"
		"\"devices\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
		"\"description\":\"/dev paths (files or directories) to list afterwards, "
		"e.g. graphics, bus/usb, net\"}},"
		"\"required\":[\"source\"]}", DriverDeploy);
	server.AddTool("driver_status",
		"Where a driver or add-on is installed (packaged and non-packaged copies "
		"with SHA-256), whether the kernel has it loaded, and which /dev entries "
		"and drivers/dev links carry its name.",
		"{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},"
		"\"kind\":{\"type\":\"string\",\"description\":\"default driver\"},"
		"\"location\":{\"type\":\"string\",\"enum\":[\"system\",\"home\"]},"
		"\"devices\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
		"\"description\":\"/dev paths (files or directories) to list afterwards, "
		"e.g. graphics, bus/usb, net\"}},"
		"\"required\":[\"name\"]}", DriverStatus);
}
