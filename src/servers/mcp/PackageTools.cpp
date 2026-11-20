/*
 * airos_mcp - packages: what is installed, install and uninstall with the
 * known traps called out.
 * Copyright 2026 air/OS contributors. MIT license.
 */
#include <algorithm>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "Mcp.h"
#include "Process.h"
#include "Tools.h"
#include "Util.h"


namespace {

JsonValue
ListPackageDirectory(const std::string& dir, const std::string& filter)
{
	JsonValue out = JsonValue::Array();
	DIR* d = opendir(dir.c_str());
	if (d == NULL)
		return out;
	std::vector<std::string> names;
	while (struct dirent* entry = readdir(d)) {
		std::string name = entry->d_name;
		if (!EndsWith(name, ".hpkg"))
			continue;
		if (!filter.empty() && strcasestr(name.c_str(), filter.c_str()) == NULL)
			continue;
		names.push_back(name);
	}
	closedir(d);
	std::sort(names.begin(), names.end());
	for (size_t i = 0; i < names.size(); i++) {
		std::string base = names[i].substr(0, names[i].size() - 5);
		JsonValue package = JsonValue::Object();
		// name-version-arch
		size_t dash = base.find('-');
		size_t lastDash = base.rfind('-');
		if (dash != std::string::npos && lastDash != dash) {
			package.Set("name", base.substr(0, dash));
			package.Set("version", base.substr(dash + 1, lastDash - dash - 1));
			package.Set("architecture", base.substr(lastDash + 1));
		} else
			package.Set("name", base);
		package.Set("file", dir + "/" + names[i]);
		struct stat st;
		if (stat((dir + "/" + names[i]).c_str(), &st) == 0) {
			package.Set("size", (int64_t)st.st_size);
			package.Set("mtime", (int64_t)st.st_mtime);
		}
		out.Push(package);
	}
	return out;
}


ToolResult
PackageList(const JsonValue& args)
{
	std::string filter = args.GetString("filter");
	JsonValue result = JsonValue::Object();
	result.Set("system", ListPackageDirectory("/boot/system/packages", filter));
	result.Set("home", ListPackageDirectory("/boot/home/config/packages", filter));
	JsonValue state = JsonValue::Array();
	DIR* d = opendir("/boot/system/packages/administrative");
	if (d != NULL) {
		while (struct dirent* entry = readdir(d)) {
			if (StartsWith(entry->d_name, "state_"))
				state.Push(entry->d_name);
		}
		closedir(d);
	}
	result.Set("old_states", state);
	result.Set("hint", "old_states are earlier activations the boot loader can "
		"fall back to");
	return ToolResult::Json(result);
}


JsonValue
PackageInfoFor(const std::string& path, std::string& name)
{
	RunResult run = RunShell("package list -i '" + path + "'", 30000000);
	JsonValue info = JsonValue::Object();
	info.Set("raw", TrimString(run.output));
	std::vector<std::string> lines = SplitLines(run.output);
	JsonValue provides = JsonValue::Array();
	JsonValue requirements = JsonValue::Array();
	std::string section;
	for (size_t i = 0; i < lines.size(); i++) {
		std::string line = TrimString(lines[i]);
		if (StartsWith(line, "name:")) {
			name = TrimString(line.substr(5));
			info.Set("name", name);
		} else if (StartsWith(line, "version:"))
			info.Set("version", TrimString(line.substr(8)));
		else if (StartsWith(line, "architecture:"))
			info.Set("architecture", TrimString(line.substr(13)));
		else if (StartsWith(line, "summary:"))
			info.Set("summary", TrimString(line.substr(8)));
		else if (StartsWith(line, "provides:"))
			section = "provides";
		else if (StartsWith(line, "requires:"))
			section = "requires";
		else if (line.find(':') != std::string::npos && line.find(':') < 20
				&& !StartsWith(line, "lib:") && !StartsWith(line, "cmd:")
				&& !StartsWith(line, "app:") && !StartsWith(line, "devel:"))
			section.clear();
		else if (section == "provides" && !line.empty())
			provides.Push(line);
		else if (section == "requires" && !line.empty())
			requirements.Push(line);
	}
	info.Set("provides", provides);
	info.Set("requires", requirements);
	return info;
}


ToolResult
PackageInfo(const JsonValue& args)
{
	std::string path = ExpandPath(args.GetString("path"));
	if (path.empty())
		return ToolResult::Error("path is required");
	std::string name;
	JsonValue info = PackageInfoFor(path, name);
	if (args.GetBool("list_files", false)) {
		RunResult run = RunShell("package list '" + path + "'", 30000000);
		JsonValue files = JsonValue::Array();
		std::vector<std::string> lines = SplitLines(run.output);
		for (size_t i = 0; i < lines.size(); i++) {
			if (!lines[i].empty())
				files.Push(lines[i]);
		}
		info.Set("files", files);
	}
	return ToolResult::Json(info);
}


// Runs pkgman with "no" on stdin to see the plan, then with -y when allowed.
ToolResult
RunPkgman(const std::string& action, const std::string& what, bool confirm,
	const std::string& trapNote)
{
	JsonValue result = JsonValue::Object();
	RunOptions plan;
	plan.shellCommand = "pkgman " + action + " '" + what + "'";
	plan.stdinData = "no\n";
	plan.timeout = 120000000;
	plan.maxOutputBytes = 256 * 1024;
	RunResult planRun = RunProcess(plan);
	std::string planText = TrimString(planRun.output);
	result.Set("plan", planText);

	// Which packages does the plan touch?
	JsonValue changes = JsonValue::Array();
	std::vector<std::string> lines = SplitLines(planText);
	for (size_t i = 0; i < lines.size(); i++) {
		std::string line = TrimString(lines[i]);
		if (StartsWith(line, "install package ") || StartsWith(line, "uninstall package ")
				|| StartsWith(line, "upgrade package ") || StartsWith(line, "downgrade package ")
				|| StartsWith(line, "activate ") || StartsWith(line, "deactivate "))
			changes.Push(line);
	}
	result.Set("changes", changes);
	bool failed = planText.find("Encountered problems") != std::string::npos
		|| planText.find("failed") != std::string::npos
		|| planText.find("No such file") != std::string::npos
		|| planText.find("Nothing to do") != std::string::npos
		|| planText.find("nothing to do") != std::string::npos;
	if (planText.find("Nothing to do") != std::string::npos
			|| planText.find("nothing to do") != std::string::npos) {
		result.Set("done", false);
		result.Set("note", "pkgman had nothing to do");
		return ToolResult::Json(result);
	}
	if (failed) {
		result.Set("done", false);
		ToolResult out = ToolResult::Json(result);
		out.isError = true;
		return out;
	}
	bool extra = changes.Size() > 1;
	if (!confirm && extra) {
		result.Set("done", false);
		result.Set("needs_confirm", true);
		result.Set("note", "the plan changes more than the one package named" +
			std::string(trapNote.empty() ? "" : ". " + trapNote)
			+ ". Call again with confirm=true to apply it.");
		return ToolResult::Json(result);
	}
	RunOptions apply;
	apply.shellCommand = "pkgman " + action + " -y '" + what + "'";
	apply.timeout = 600000000;
	apply.maxOutputBytes = 256 * 1024;
	RunResult applyRun = RunProcess(apply);
	result.Set("output", TrimString(applyRun.output));
	result.Set("exit_code", applyRun.exitCode);
	result.Set("done", applyRun.exitCode == 0);
	if (applyRun.exitCode != 0) {
		ToolResult out = ToolResult::Json(result);
		out.isError = true;
		return out;
	}
	return ToolResult::Json(result);
}


ToolResult
PackageInstall(const JsonValue& args)
{
	std::string path = ExpandPath(args.GetString("path"));
	if (path.empty())
		return ToolResult::Error("path (an .hpkg on the device, or a package name "
			"from a repository) is required");
	bool confirm = args.GetBool("confirm", false);
	std::string note;
	if (EndsWith(path, ".hpkg")) {
		std::string name;
		JsonValue info = PackageInfoFor(path, name);
		if (name.empty())
			return ToolResult::Error("not a readable package: " + path + "\n"
				+ info.GetString("raw"));
		if (EndsWith(name, "_devel"))
			note = "this is a _devel package: installing it may pull a matching "
				"runtime and uninstalling it later removes haiku_devel with it";
		ToolResult result = RunPkgman("install", path, confirm, note);
		if (result.structured.IsObject()) {
			result.structured.Set("package", info);
			result.content = ToolResult::Json(result.structured).content;
		}
		return result;
	}
	return RunPkgman("install", path, confirm, "pkgman refresh may be broken on "
		"lab machines (repo.checksum vs repo.sha256); copy the .hpkg over instead "
		"when the repository is not reachable");
}


ToolResult
PackageUninstall(const JsonValue& args)
{
	std::string name = args.GetString("name");
	if (name.empty())
		return ToolResult::Error("name is required");
	bool confirm = args.GetBool("confirm", false);
	return RunPkgman("uninstall", name, confirm, "On this system uninstalling a "
		"_devel package has removed haiku_devel with it (the solver removes "
		"dependents); check the plan");
}

} // namespace


void
RegisterPackageTools(McpServer& server)
{
	server.AddTool("package_list",
		"Installed packages (system and home), parsed from the package "
		"directories, with the old activation states the boot loader can "
		"return to.",
		"{\"type\":\"object\",\"properties\":{\"filter\":{\"type\":\"string\"}}}",
		PackageList);
	server.AddTool("package_info",
		"Name, version, architecture, provides and requirements of an .hpkg file on "
		"the device; optionally its file list.",
		"{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
		"\"list_files\":{\"type\":\"boolean\"}},\"required\":[\"path\"]}",
		PackageInfo);
	server.AddTool("package_install",
		"Install an .hpkg from the device (upload with file_put first) or a "
		"named package from the repositories. Shows pkgman's plan first; when the "
		"plan changes more than the one package it stops and asks for "
		"confirm=true. Warns about _devel packages.",
		"{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
		"\"confirm\":{\"type\":\"boolean\"}},\"required\":[\"path\"]}",
		PackageInstall);
	server.AddTool("package_uninstall",
		"Uninstall a package by name, showing pkgman's plan first and requiring "
		"confirm=true when other packages would go with it (the solver removes "
		"dependents, which has taken haiku_devel away before).",
		"{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},"
		"\"confirm\":{\"type\":\"boolean\"}},\"required\":[\"name\"]}",
		PackageUninstall);
}
