/*
 * airos_mcp - moving files to and from the device.
 * Copyright 2026 air/OS contributors. MIT license.
 */
#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <Directory.h>
#include <Entry.h>
#include <Path.h>

#include "Mcp.h"
#include "Process.h"
#include "Tools.h"
#include "Util.h"


namespace {

bool
LooksBinary(const std::string& data)
{
	size_t check = data.size() < 4096 ? data.size() : 4096;
	for (size_t i = 0; i < check; i++) {
		unsigned char c = data[i];
		if (c == 0)
			return true;
		if (c < 0x20 && c != '\n' && c != '\r' && c != '\t' && c != '\f' && c != 0x1b)
			return true;
	}
	return false;
}


std::string
FormatTime(time_t t)
{
	char buffer[64];
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
	return buffer;
}


JsonValue
StatToJson(const std::string& path, const struct stat& st)
{
	JsonValue entry = JsonValue::Object();
	entry.Set("path", path);
	const char* type = "other";
	if (S_ISREG(st.st_mode)) type = "file";
	else if (S_ISDIR(st.st_mode)) type = "directory";
	else if (S_ISLNK(st.st_mode)) type = "symlink";
	else if (S_ISCHR(st.st_mode)) type = "device";
	else if (S_ISFIFO(st.st_mode)) type = "fifo";
	entry.Set("type", type);
	entry.Set("size", (int64_t)st.st_size);
	entry.Set("mode", Format("%04o", (unsigned)(st.st_mode & 07777)));
	entry.Set("modified", FormatTime(st.st_mtime));
	entry.Set("mtime", (int64_t)st.st_mtime);
	if (S_ISLNK(st.st_mode)) {
		char target[B_PATH_NAME_LENGTH];
		ssize_t length = readlink(path.c_str(), target, sizeof(target) - 1);
		if (length > 0) {
			target[length] = 0;
			entry.Set("target", target);
		}
	}
	return entry;
}


void
ListDirectory(const std::string& path, int depth, bool hidden, JsonValue& out,
	int& count, int limit)
{
	DIR* dir = opendir(path.c_str());
	if (dir == NULL)
		return;
	std::vector<std::string> names;
	while (struct dirent* entry = readdir(dir)) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (!hidden && entry->d_name[0] == '.')
			continue;
		names.push_back(entry->d_name);
	}
	closedir(dir);
	std::sort(names.begin(), names.end());
	for (size_t i = 0; i < names.size(); i++) {
		if (count >= limit)
			return;
		std::string full = path + (EndsWith(path, "/") ? "" : "/") + names[i];
		struct stat st;
		if (lstat(full.c_str(), &st) != 0)
			continue;
		out.Push(StatToJson(full, st));
		count++;
		if (S_ISDIR(st.st_mode) && depth > 1)
			ListDirectory(full, depth - 1, hidden, out, count, limit);
	}
}


ToolResult
FileList(const JsonValue& args)
{
	std::string path = ExpandPath(args.GetString("path", "/boot/home"));
	int depth = (int)args.GetInt("depth", 1);
	bool hidden = args.GetBool("include_hidden", false);
	int limit = (int)args.GetInt("limit", 500);
	struct stat st;
	if (lstat(path.c_str(), &st) != 0)
		return ToolResult::Error(Format("%s: %s", path.c_str(), strerror(errno)));
	JsonValue result = JsonValue::Object();
	result.Set("entry", StatToJson(path, st));
	if (S_ISDIR(st.st_mode)) {
		JsonValue entries = JsonValue::Array();
		int count = 0;
		ListDirectory(path, depth, hidden, entries, count, limit);
		result.Set("entries", entries);
		if (count >= limit)
			result.Set("truncated", true);
	}
	return ToolResult::Json(result);
}


ToolResult
FileGet(const JsonValue& args)
{
	std::string path = ExpandPath(args.GetString("path"));
	if (path.empty())
		return ToolResult::Error("path is required");
	int64_t offset = args.GetInt("offset", 0);
	int64_t maxBytes = args.GetInt("max_bytes", 1024 * 1024);
	std::string encoding = args.GetString("encoding", "auto");
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return ToolResult::Error(Format("%s: %s", path.c_str(), strerror(errno)));
	std::string data;
	if (!ReadFileToString(path, data, maxBytes, offset))
		return ToolResult::Error(Format("cannot read %s: %s", path.c_str(),
			strerror(errno)));
	JsonValue result = JsonValue::Object();
	result.Set("path", path);
	result.Set("size", (int64_t)st.st_size);
	result.Set("offset", offset);
	result.Set("returned", (int64_t)data.size());
	result.Set("complete", offset + (int64_t)data.size() >= (int64_t)st.st_size);
	bool binary = encoding == "base64" || (encoding == "auto" && LooksBinary(data));
	if (binary) {
		result.Set("encoding", "base64");
		result.Set("data", Base64Encode(data.data(), data.size()));
	} else {
		result.Set("encoding", "text");
		result.Set("text", data);
	}
	return ToolResult::Json(result);
}


ToolResult
FilePut(const JsonValue& args)
{
	std::string path = ExpandPath(args.GetString("path"));
	if (path.empty())
		return ToolResult::Error("path is required");
	std::string data;
	if (args.Has("base64")) {
		if (!Base64Decode(args.GetString("base64"), data))
			return ToolResult::Error("base64 is not valid");
	} else if (args.Has("text"))
		data = args.GetString("text");
	else
		return ToolResult::Error("text or base64 is required");
	bool append = args.GetBool("append", false);
	bool mkdirs = args.GetBool("mkdirs", true);
	std::string modeText = args.GetString("mode");
	mode_t mode = modeText.empty() ? 0644 : (mode_t)strtoul(modeText.c_str(), NULL, 8);
	bool atomic = args.GetBool("atomic", true);

	if (mkdirs) {
		BPath parent(path.c_str());
		BPath dir;
		if (parent.GetParent(&dir) == B_OK)
			create_directory(dir.Path(), 0755);
	}

	std::string target = path;
	if (append) {
		int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, mode);
		if (fd < 0)
			return ToolResult::Error(Format("cannot open %s: %s", path.c_str(),
				strerror(errno)));
		ssize_t written = write(fd, data.data(), data.size());
		close(fd);
		if (written != (ssize_t)data.size())
			return ToolResult::Error("short write");
	} else {
		// A new file renamed into place: a program running from the old one
		// keeps it, where writing over it changes the code it is running.
		std::string temp = atomic ? path + ".airos_mcp.new" : path;
		if (!WriteStringToFile(temp, data, mode))
			return ToolResult::Error(Format("cannot write %s: %s", temp.c_str(),
				strerror(errno)));
		chmod(temp.c_str(), mode);
		if (atomic && rename(temp.c_str(), path.c_str()) != 0) {
			std::string error = strerror(errno);
			unlink(temp.c_str());
			return ToolResult::Error(Format("cannot rename into %s: %s", path.c_str(),
				error.c_str()));
		}
	}
	struct stat st;
	stat(path.c_str(), &st);
	JsonValue result = StatToJson(path, st);
	result.Set("written", (int64_t)data.size());
	result.Set("appended", append);
	return ToolResult::Json(result);
}


ToolResult
FileHash(const JsonValue& args)
{
	std::string path = ExpandPath(args.GetString("path"));
	if (path.empty())
		return ToolResult::Error("path is required");
	RunResult run = RunShell("sha256sum '" + path + "'");
	if (run.exitCode != 0)
		return ToolResult::Error(TrimString(run.output));
	JsonValue result = JsonValue::Object();
	result.Set("path", path);
	std::string sum = TrimString(run.output);
	size_t space = sum.find(' ');
	result.Set("sha256", space == std::string::npos ? sum : sum.substr(0, space));
	struct stat st;
	if (stat(path.c_str(), &st) == 0)
		result.Set("size", (int64_t)st.st_size);
	return ToolResult::Json(result);
}

} // namespace


void
RegisterFileTools(McpServer& server)
{
	server.AddTool("file_list",
		"List a directory on the device (or stat one file): type, size, mode, "
		"modification time, symlink target. depth > 1 recurses.",
		"{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
		"\"depth\":{\"type\":\"integer\",\"description\":\"default 1\"},"
		"\"include_hidden\":{\"type\":\"boolean\"},"
		"\"limit\":{\"type\":\"integer\",\"description\":\"max entries (default 500)\"}},"
		"\"required\":[\"path\"]}", FileList);
	server.AddTool("file_get",
		"Read a file from the device. Text comes back as text, binary as base64 "
		"(or force with encoding). offset and max_bytes page through big files.",
		"{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
		"\"offset\":{\"type\":\"integer\"},\"max_bytes\":{\"type\":\"integer\","
		"\"description\":\"default 1048576\"},\"encoding\":{\"type\":\"string\","
		"\"enum\":[\"auto\",\"text\",\"base64\"]}},\"required\":[\"path\"]}", FileGet);
	server.AddTool("file_put",
		"Write a file on the device from text or base64. Written to a temporary "
		"name and renamed into place (a program running from the old file keeps "
		"its copy), parent directories created, mode set (octal string, default "
		"0644). Use it to upload a driver, a test program or a settings file "
		"before driver_deploy, run_command or package_install.",
		"{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
		"\"text\":{\"type\":\"string\"},\"base64\":{\"type\":\"string\"},"
		"\"mode\":{\"type\":\"string\",\"description\":\"octal, e.g. 0755\"},"
		"\"append\":{\"type\":\"boolean\"},\"mkdirs\":{\"type\":\"boolean\","
		"\"description\":\"default true\"},\"atomic\":{\"type\":\"boolean\","
		"\"description\":\"rename into place (default true)\"}},"
		"\"required\":[\"path\"]}", FilePut);
	server.AddTool("file_hash", "SHA-256 and size of a file on the device, to "
		"verify an upload or compare with a build.",
		"{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},"
		"\"required\":[\"path\"]}", FileHash);
}
