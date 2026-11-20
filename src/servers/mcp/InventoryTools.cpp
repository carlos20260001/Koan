/*
 * airos_mcp - what hardware the machine has and what drives it.
 * Copyright 2026 air/OS contributors. MIT license.
 */
#include <algorithm>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>

#include <MediaAddOn.h>
#include <MediaRoster.h>
#include <OS.h>

#include "Mcp.h"
#include "Process.h"
#include "Tools.h"
#include "Util.h"


namespace {

JsonValue
SystemSection()
{
	JsonValue out = JsonValue::Object();
	system_info info;
	if (get_system_info(&info) == B_OK) {
		out.Set("cpu_count", (int64_t)info.cpu_count);
		out.Set("max_pages", (int64_t)info.max_pages);
		out.Set("used_pages", (int64_t)info.used_pages);
		out.Set("cached_pages", (int64_t)info.cached_pages);
		out.Set("memory_mb", (int64_t)(info.max_pages * B_PAGE_SIZE / (1024 * 1024)));
		out.Set("used_memory_mb", (int64_t)(info.used_pages * B_PAGE_SIZE / (1024 * 1024)));
		out.Set("free_memory_mb", (int64_t)(info.free_memory / (1024 * 1024)));
		out.Set("page_faults", (int64_t)info.page_faults);
		out.Set("used_threads", (int64_t)info.used_threads);
		out.Set("used_teams", (int64_t)info.used_teams);
		out.Set("used_sems", (int64_t)info.used_sems);
		out.Set("used_ports", (int64_t)info.used_ports);
		out.Set("kernel_name", info.kernel_name);
		out.Set("kernel_build", std::string(info.kernel_build_date) + " "
			+ info.kernel_build_time);
		out.Set("kernel_version", (int64_t)info.kernel_version);
		time_t boot = info.boot_time / 1000000;
		char buffer[64];
		struct tm tm;
		localtime_r(&boot, &tm);
		strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
		out.Set("boot_time", buffer);
		out.Set("uptime_s", (int64_t)(system_time() / 1000000));
	}
	struct utsname uts;
	if (uname(&uts) == 0) {
		out.Set("hostname", uts.nodename);
		out.Set("release", uts.release);
		out.Set("version", uts.version);
		out.Set("machine", uts.machine);
	}
	cpu_topology_node_info* topology = NULL;
	uint32 count = 0;
	if (get_cpu_topology_info(NULL, &count) == B_OK && count > 0) {
		topology = new cpu_topology_node_info[count];
		if (get_cpu_topology_info(topology, &count) == B_OK) {
			for (uint32 i = 0; i < count; i++) {
				if (topology[i].type == B_TOPOLOGY_CORE) {
					out.Set("core_frequency_mhz",
						(int64_t)(topology[i].data.core.default_frequency / 1000000));
					break;
				}
			}
			for (uint32 i = 0; i < count; i++) {
				if (topology[i].type == B_TOPOLOGY_PACKAGE) {
					out.Set("cpu_vendor", (int64_t)topology[i].data.package.vendor);
					break;
				}
			}
		}
		delete[] topology;
	}
	RunResult run = RunShell("sysinfo -cpu 2>/dev/null | head -3", 10000000);
	std::vector<std::string> lines = SplitLines(run.output);
	for (size_t i = 0; i < lines.size(); i++) {
		if (lines[i].find("running at") != std::string::npos || i == 1) {
			out.Set("cpu", TrimString(lines[i]));
			break;
		}
	}
	return out;
}


JsonValue
DevicesSection()
{
	RunResult run = RunShell("listdev", 30000000);
	JsonValue devices = JsonValue::Array();
	JsonValue current;
	std::vector<std::string> lines = SplitLines(run.output);
	for (size_t i = 0; i < lines.size(); i++) {
		const std::string& line = lines[i];
		if (line.empty())
			continue;
		if (line[0] != ' ' && line[0] != '\t') {
			if (current.IsObject())
				devices.Push(current);
			current = JsonValue::Object();
			std::string header = line;
			if (StartsWith(header, "device "))
				header.erase(0, 7);
			current.Set("class", header);
		} else if (current.IsObject()) {
			std::string attr = TrimString(line);
			size_t space = attr.find(' ');
			std::string key = space == std::string::npos ? attr : attr.substr(0, space);
			std::string value = space == std::string::npos ? "" : TrimString(attr.substr(space));
			if (key == "vendor" || key == "device" || key == "subsystem"
					|| key == "class" || key == "revision")
				current.Set(key, value);
			else {
				JsonValue& extra = current["other"];
				if (!extra.IsArray())
					extra = JsonValue::Array();
				extra.Push(attr);
			}
		}
	}
	if (current.IsObject())
		devices.Push(current);
	JsonValue out = JsonValue::Object();
	out.Set("devices", devices);
	if (run.exitCode != 0)
		out.Set("listdev_error", TrimString(run.output));
	return out;
}


JsonValue
UsbSection()
{
	RunResult run = RunShell("listusb", 30000000);
	JsonValue devices = JsonValue::Array();
	std::vector<std::string> lines = SplitLines(run.output);
	for (size_t i = 0; i < lines.size(); i++) {
		std::string line = TrimString(lines[i]);
		if (!line.empty())
			devices.Push(line);
	}
	JsonValue out = JsonValue::Object();
	out.Set("devices", devices);
	out.Set("hint", "run_command \"listusb -v\" for descriptors of one device");
	return out;
}


JsonValue
NetworkSection()
{
	RunResult run = RunShell("ifconfig", 20000000);
	JsonValue interfaces = JsonValue::Array();
	JsonValue current;
	std::vector<std::string> lines = SplitLines(run.output);
	for (size_t i = 0; i < lines.size(); i++) {
		const std::string& line = lines[i];
		if (line.empty())
			continue;
		if (line[0] != ' ' && line[0] != '\t') {
			if (current.IsObject())
				interfaces.Push(current);
			current = JsonValue::Object();
			current.Set("name", TrimString(line));
			current.Set("addresses", JsonValue::Array());
		} else if (current.IsObject()) {
			std::string text = TrimString(line);
			if (StartsWith(text, "Hardware type:")) {
				size_t address = text.find("Address:");
				if (address != std::string::npos)
					current.Set("mac", TrimString(text.substr(address + 8)));
				std::string hardware = TrimString(text.substr(14,
					address == std::string::npos ? std::string::npos : address - 14));
				if (!hardware.empty() && hardware[hardware.size() - 1] == ',')
					hardware.erase(hardware.size() - 1);
				current.Set("hardware", hardware);
			} else if (StartsWith(text, "Media type:"))
				current.Set("media", TrimString(text.substr(11)));
			else if (StartsWith(text, "inet addr:") || StartsWith(text, "inet6 addr:"))
				current["addresses"].Push(text);
			else if (StartsWith(text, "MTU:")) {
				current.Set("flags", text);
				current.Set("up", text.find(" up") != std::string::npos);
				current.Set("link", text.find("link") != std::string::npos);
			} else if (StartsWith(text, "Receive:") || StartsWith(text, "Transmit:"))
				current.Set(StartsWith(text, "Receive:") ? "receive" : "transmit", text);
			else {
				JsonValue& extra = current["other"];
				if (!extra.IsArray())
					extra = JsonValue::Array();
				extra.Push(text);
			}
		}
	}
	if (current.IsObject())
		interfaces.Push(current);
	JsonValue out = JsonValue::Object();
	out.Set("interfaces", interfaces);
	return out;
}


JsonValue
MediaSection()
{
	JsonValue out = JsonValue::Object();
	const char* reason = NULL;
	if (!EnsureApplication(&reason)) {
		out.Set("error", reason);
		return out;
	}
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		out.Set("error", "media_server is not running");
		return out;
	}
	live_node_info nodes[128];
	int32 count = 128;
	status_t status = roster->GetLiveNodes(nodes, &count);
	if (status != B_OK) {
		out.Set("error", "GetLiveNodes: " + StrError(status));
		return out;
	}
	JsonValue list = JsonValue::Array();
	for (int32 i = 0; i < count; i++) {
		JsonValue node = JsonValue::Object();
		node.Set("name", nodes[i].name);
		node.Set("id", (int64_t)nodes[i].node.node);
		JsonValue kinds = JsonValue::Array();
		uint64 kind = nodes[i].node.kind;
		if (kind & B_BUFFER_PRODUCER) kinds.Push("producer");
		if (kind & B_BUFFER_CONSUMER) kinds.Push("consumer");
		if (kind & B_TIME_SOURCE) kinds.Push("time source");
		if (kind & B_CONTROLLABLE) kinds.Push("controllable");
		if (kind & B_FILE_INTERFACE) kinds.Push("file interface");
		if (kind & B_ENTITY_INTERFACE) kinds.Push("entity interface");
		if (kind & B_PHYSICAL_INPUT) kinds.Push("physical input");
		if (kind & B_PHYSICAL_OUTPUT) kinds.Push("physical output");
		if (kind & B_SYSTEM_MIXER) kinds.Push("system mixer");
		node.Set("kinds", kinds);
		dormant_node_info dormant;
		if (roster->GetDormantNodeFor(nodes[i].node, &dormant) == B_OK)
			node.Set("add_on", dormant.name);
		list.Push(node);
	}
	out.Set("nodes", list);
	media_node input, output;
	if (roster->GetAudioInput(&input) == B_OK)
		out.Set("default_audio_input", (int64_t)input.node);
	if (roster->GetAudioOutput(&output) == B_OK)
		out.Set("default_audio_output", (int64_t)output.node);
	media_node video;
	if (roster->GetVideoInput(&video) == B_OK)
		out.Set("default_video_input", (int64_t)video.node);
	return out;
}


JsonValue
KernelModulesSection()
{
	JsonValue out = JsonValue::Object();
	JsonValue images = JsonValue::Array();
	int32 cookie = 0;
	image_info info;
	while (get_next_image_info(B_SYSTEM_TEAM, &cookie, &info) == B_OK) {
		JsonValue image = JsonValue::Object();
		image.Set("id", (int64_t)info.id);
		image.Set("name", info.name);
		image.Set("text_size", (int64_t)info.text_size);
		image.Set("data_size", (int64_t)info.data_size);
		images.Push(image);
	}
	out.Set("images", images);
	out.Set("count", (int64_t)images.Size());
	return out;
}


void
WalkDev(const std::string& dir, JsonValue& out, int depth, int maxDepth)
{
	DIR* d = opendir(dir.c_str());
	if (d == NULL)
		return;
	std::vector<std::string> names;
	while (struct dirent* entry = readdir(d)) {
		if (entry->d_name[0] != '.')
			names.push_back(entry->d_name);
	}
	closedir(d);
	std::sort(names.begin(), names.end());
	for (size_t i = 0; i < names.size(); i++) {
		std::string path = dir + "/" + names[i];
		struct stat st;
		if (lstat(path.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth < maxDepth)
				WalkDev(path, out, depth + 1, maxDepth);
			else
				out.Push(path + "/");
		} else
			out.Push(path);
	}
}


JsonValue
DevTreeSection(const std::string& root)
{
	JsonValue out = JsonValue::Object();
	JsonValue entries = JsonValue::Array();
	WalkDev(root, entries, 0, 6);
	out.Set("root", root);
	out.Set("entries", entries);
	return out;
}


JsonValue
DisplaysSection()
{
	JsonValue out = JsonValue::Object();
	RunResult run = RunShell("screenmode -d 2>&1; echo ---; screenmode 2>&1", 10000000);
	out.Set("screenmode", TrimString(run.output));
	return out;
}


JsonValue
DisksSection()
{
	JsonValue out = JsonValue::Object();
	RunResult run = RunShell("df -h 2>&1", 10000000);
	out.Set("df", TrimString(run.output));
	return out;
}


ToolResult
HwInventory(const JsonValue& args)
{
	std::vector<std::string> sections;
	const JsonValue* wanted = args.Get("sections");
	if (wanted != NULL && wanted->IsArray() && wanted->Size() > 0) {
		for (size_t i = 0; i < wanted->Size(); i++)
			sections.push_back(wanted->At(i).AsString());
	} else {
		const char* all[] = { "system", "devices", "usb", "network", "media",
			"displays", "disks", NULL };
		for (int i = 0; all[i] != NULL; i++)
			sections.push_back(all[i]);
	}
	JsonValue result = JsonValue::Object();
	for (size_t i = 0; i < sections.size(); i++) {
		const std::string& s = sections[i];
		if (s == "system") result.Set(s, SystemSection());
		else if (s == "devices") result.Set(s, DevicesSection());
		else if (s == "usb") result.Set(s, UsbSection());
		else if (s == "network") result.Set(s, NetworkSection());
		else if (s == "media") result.Set(s, MediaSection());
		else if (s == "kernel_modules") result.Set(s, KernelModulesSection());
		else if (s == "dev_tree") result.Set(s, DevTreeSection(args.GetString("dev_root", "/dev")));
		else if (s == "displays") result.Set(s, DisplaysSection());
		else if (s == "disks") result.Set(s, DisksSection());
		else result.Set(s, JsonValue("unknown section (system, devices, usb, network, "
			"media, kernel_modules, dev_tree, displays, disks)"));
	}
	return ToolResult::Json(result);
}

} // namespace


void
RegisterInventoryTools(McpServer& server)
{
	server.AddTool("hw_inventory",
		"Structured hardware and driver inventory of the device: system (CPU, "
		"memory, kernel build, uptime, hrev), devices (listdev parsed: PCI class, "
		"vendor, device), usb (listusb), network (ifconfig parsed per interface "
		"with addresses, media, link flags), media (live Media Kit nodes and the "
		"default inputs/outputs), kernel_modules (every image loaded in the "
		"kernel: drivers, bus managers, file systems), dev_tree (the published "
		"/dev entries), displays (screenmode), disks (df). Default sections: all "
		"but kernel_modules and dev_tree.",
		"{\"type\":\"object\",\"properties\":{\"sections\":{\"type\":\"array\","
		"\"items\":{\"type\":\"string\"},\"description\":\"which sections\"},"
		"\"dev_root\":{\"type\":\"string\",\"description\":\"for dev_tree: start "
		"here (default /dev)\"}}}", HwInventory);
}
