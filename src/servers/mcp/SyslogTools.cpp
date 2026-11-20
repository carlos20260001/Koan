/*
 * airos_mcp - syslog queries by boot, pattern and mark.
 * Copyright 2026 air/OS contributors. MIT license.
 */
#include <regex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "Mcp.h"
#include "Tools.h"
#include "Util.h"


// TODO(linux backend): on Linux the log is the systemd journal; this file
// is written only by syslog_daemon (which reads the journal) where it may
// write /var/log. Querying the journal itself (sd_journal) would not need it.
static const char* kSyslogPath = "/var/log/syslog";
static const char* kOldSyslogPath = "/var/log/syslog.1";
// The kernel writes this first thing on every boot.
static const char* kBootMarker = "Welcome to kernel debugger output!";


namespace {

struct SyslogMark {
	ino_t inode;
	off_t size;
	off_t oldSize;
	bool valid;
	SyslogMark() : inode(0), size(0), oldSize(0), valid(false) {}
};


SyslogMark
CurrentMark()
{
	SyslogMark mark;
	struct stat st;
	if (stat(kSyslogPath, &st) == 0) {
		mark.inode = st.st_ino;
		mark.size = st.st_size;
		mark.valid = true;
	}
	if (stat(kOldSyslogPath, &st) == 0)
		mark.oldSize = st.st_size;
	return mark;
}


std::string
MarkToString(const SyslogMark& mark)
{
	return Format("%lld:%lld:%lld", (long long)mark.inode, (long long)mark.size,
		(long long)mark.oldSize);
}


SyslogMark
ParseMark(const std::string& text)
{
	SyslogMark mark;
	long long inode = 0, size = 0, oldSize = 0;
	if (sscanf(text.c_str(), "%lld:%lld:%lld", &inode, &size, &oldSize) >= 2) {
		mark.inode = inode;
		mark.size = size;
		mark.oldSize = oldSize;
		mark.valid = true;
	}
	return mark;
}


// Everything written since the mark, following a rotation when there was one.
std::string
ReadSinceMark(const SyslogMark& mark, std::string& note)
{
	SyslogMark now = CurrentMark();
	std::string text;
	if (!now.valid)
		return text;
	if (now.inode == mark.inode && now.size >= mark.size) {
		ReadFileToString(kSyslogPath, text, 0, mark.size);
		return text;
	}
	// Rotated: the marked file is now syslog.1 (when it still is that file).
	std::string old;
	struct stat st;
	if (stat(kOldSyslogPath, &st) == 0 && (off_t)st.st_size >= mark.size) {
		ReadFileToString(kOldSyslogPath, old, 0, mark.size);
		note = "the syslog was rotated since the mark; syslog.1 was included";
	} else
		note = "the syslog was rotated since the mark and the older file is gone "
			"or shorter than expected; some lines may be missing";
	ReadFileToString(kSyslogPath, text);
	return old + text;
}


// Splits the complete log into boots, oldest first; the last is the
// current boot.
std::vector<std::vector<std::string> >
SplitBoots(const std::vector<std::string>& lines)
{
	std::vector<std::vector<std::string> > boots;
	boots.push_back(std::vector<std::string>());
	for (size_t i = 0; i < lines.size(); i++) {
		if (lines[i].find(kBootMarker) != std::string::npos) {
			if (!boots.back().empty())
				boots.push_back(std::vector<std::string>());
		}
		boots.back().push_back(lines[i]);
	}
	return boots;
}


ToolResult
SyslogQuery(const JsonValue& args)
{
	std::string pattern = args.GetString("pattern");
	std::string boot = args.GetString("boot", "current");
	int64_t tail = args.GetInt("tail", 200);
	int64_t context = args.GetInt("context", 0);
	bool ignoreCase = args.GetBool("ignore_case", true);
	std::string sinceMark = args.GetString("since_mark");
	bool invert = args.GetBool("invert", false);
	int64_t maxBytes = args.GetInt("max_bytes", 200 * 1024);

	std::string note;
	std::string text;
	JsonValue files = JsonValue::Array();
	if (!sinceMark.empty()) {
		SyslogMark mark = ParseMark(sinceMark);
		if (!mark.valid)
			return ToolResult::Error("since_mark is not a mark from syslog_mark or "
				"an earlier syslog_query");
		text = ReadSinceMark(mark, note);
		files.Push("since mark");
		boot = "all";
	} else {
		// every old file there is, oldest first (syslog.4 ... syslog.1;
		// air/OS keeps four), then the current one
		for (int i = 9; i >= 1; i--) {
			std::string path = std::string(kSyslogPath) + "." + std::to_string(i);
			std::string old;
			if (ReadFileToString(path.c_str(), old)) {
				files.Push(path);
				text += old;
			}
		}
		std::string current;
		if (ReadFileToString(kSyslogPath, current))
			files.Push(kSyslogPath);
		text += current;
	}

	std::vector<std::string> lines = SplitLines(text);
	std::vector<std::vector<std::string> > boots = SplitBoots(lines);
	std::vector<std::string> selected;
	int bootsFound = 0;
	for (size_t i = 0; i < boots.size(); i++) {
		if (!boots[i].empty() && boots[i][0].find(kBootMarker) != std::string::npos)
			bootsFound++;
	}
	if (boot == "all")
		selected = lines;
	else {
		int index = 0;		// 0 = current, 1 = previous, ...
		if (boot == "current")
			index = 0;
		else if (boot == "previous")
			index = 1;
		else
			index = atoi(boot.c_str());
		if (index < 0 || index >= (int)boots.size())
			return ToolResult::Error(Format("boot %s is not in the log: %d boot%s "
				"found (markers: %d)", boot.c_str(), (int)boots.size(),
				boots.size() == 1 ? "" : "s", bootsFound));
		selected = boots[boots.size() - 1 - index];
		if (bootsFound == 0 && note.empty())
			note = "no boot marker in the log files (they were rotated since boot); "
				"treating everything as the current boot";
	}

	std::vector<size_t> matches;
	std::string regexError;
	if (!pattern.empty()) {
		try {
			std::regex::flag_type flags = std::regex::ECMAScript | std::regex::optimize;
			if (ignoreCase)
				flags |= std::regex::icase;
			std::regex re(pattern, flags);
			for (size_t i = 0; i < selected.size(); i++) {
				bool hit = std::regex_search(selected[i], re);
				if (hit != invert)
					matches.push_back(i);
			}
		} catch (std::regex_error& e) {
			return ToolResult::Error(Format("bad pattern: %s", e.what()));
		}
	} else {
		for (size_t i = 0; i < selected.size(); i++)
			matches.push_back(i);
	}

	// Expand context, then keep the tail.
	std::vector<bool> keep(selected.size(), false);
	for (size_t m = 0; m < matches.size(); m++) {
		size_t from = matches[m] >= (size_t)context ? matches[m] - context : 0;
		size_t to = matches[m] + context;
		if (to >= selected.size())
			to = selected.size() - 1;
		for (size_t i = from; i <= to; i++)
			keep[i] = true;
	}
	std::vector<std::string> out;
	for (size_t i = 0; i < selected.size(); i++) {
		if (keep[i])
			out.push_back(selected[i]);
	}
	size_t total = out.size();
	if (tail > 0 && out.size() > (size_t)tail)
		out.erase(out.begin(), out.end() - tail);

	JsonValue result = JsonValue::Object();
	JsonValue outLines = JsonValue::Array();
	size_t bytes = 0;
	bool truncated = false;
	for (size_t i = 0; i < out.size(); i++) {
		bytes += out[i].size() + 1;
		if (maxBytes > 0 && bytes > (size_t)maxBytes) {
			truncated = true;
			break;
		}
		outLines.Push(out[i]);
	}
	result.Set("lines", outLines);
	result.Set("matched", (int64_t)matches.size());
	result.Set("returned", (int64_t)outLines.Size());
	result.Set("selected_lines", (int64_t)selected.size());
	result.Set("omitted_before_tail", (int64_t)(total - out.size()));
	result.Set("boots_in_log", (int64_t)boots.size());
	result.Set("boot_markers", (int64_t)bootsFound);
	result.Set("boot", boot);
	result.Set("files", files);
	result.Set("mark", MarkToString(CurrentMark()));
	if (truncated)
		result.Set("truncated_by_max_bytes", true);
	if (!note.empty())
		result.Set("note", note);
	result.Set("hint", "air/OS syslog lines start with the local time they were "
		"written (kernel lines: when the syslog daemon got them), unless "
		"syslog_time_stamps is off in the kernel settings. Use the mark with "
		"since_mark to read only what was logged after this call.");
	return ToolResult::Json(result);
}


ToolResult
SyslogMarkTool(const JsonValue& args)
{
	JsonValue result = JsonValue::Object();
	SyslogMark mark = CurrentMark();
	result.Set("mark", MarkToString(mark));
	result.Set("syslog_bytes", (int64_t)mark.size);
	result.Set("usage", "pass as since_mark to syslog_query to get only lines "
		"written after now");
	return ToolResult::Json(result);
}


ToolResult
SyslogWrite(const JsonValue& args)
{
	std::string text = args.GetString("text");
	if (text.empty())
		return ToolResult::Error("text is required");
	FILE* f = fopen(kSyslogPath, "a");
	if (f == NULL)
		return ToolResult::Error(Format("cannot open %s: %s", kSyslogPath,
			strerror(errno)));
	fprintf(f, "airos_mcp: MARK %s\n", text.c_str());
	fclose(f);
	return ToolResult::Text(Format("wrote \"airos_mcp: MARK %s\" to the syslog",
		text.c_str()));
}

} // namespace


void
RegisterSyslogTools(McpServer& server)
{
	server.AddTool("syslog_query",
		"Read the system log (/var/log/syslog and its rotated syslog.N files) on "
		"the device. Selects one boot (default the current one: the "
		"file persists across warm reboots, so filtering by boot matters), filters "
		"lines by a regular expression with optional context, and returns the "
		"tail. Every call also returns a mark; pass it later as since_mark to get "
		"only what was logged in between, across a rotation.",
		"{\"type\":\"object\",\"properties\":{"
		"\"pattern\":{\"type\":\"string\",\"description\":\"ECMAScript regular "
		"expression; lines matching it are returned (all lines when omitted)\"},"
		"\"boot\":{\"type\":\"string\",\"description\":\"current (default), "
		"previous, all, or a number: 0 = current boot, 1 = the one before\"},"
		"\"tail\":{\"type\":\"integer\",\"description\":\"return at most this many "
		"of the last matching lines (default 200; 0 = all)\"},"
		"\"context\":{\"type\":\"integer\",\"description\":\"lines of context "
		"around each match (default 0)\"},"
		"\"ignore_case\":{\"type\":\"boolean\",\"description\":\"default true\"},"
		"\"invert\":{\"type\":\"boolean\",\"description\":\"return lines NOT "
		"matching the pattern\"},"
		"\"since_mark\":{\"type\":\"string\",\"description\":\"a mark from an "
		"earlier call: only lines written after it (boot is then ignored)\"},"
		"\"max_bytes\":{\"type\":\"integer\",\"description\":\"cap on returned "
		"text (default 204800)\"}}}",
		SyslogQuery);
	server.AddTool("syslog_mark",
		"Return a mark for the current end of the syslog, to pass as since_mark to "
		"syslog_query after running something.",
		"{\"type\":\"object\",\"properties\":{}}", SyslogMarkTool);
	server.AddTool("syslog_write",
		"Append a marker line \"airos_mcp: MARK <text>\" to the syslog so that "
		"later syslog_query output can be related to a test step.",
		"{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},"
		"\"required\":[\"text\"]}", SyslogWrite);
}
