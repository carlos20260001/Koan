/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <DesktopEntryLauncher.h>

#include <spawn.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include <DesktopEntry.h>
#include <Path.h>


extern char** environ;


static status_t
tokenize_exec(const char* exec, std::vector<std::string>& tokens)
{
	std::string token;
	bool quoted = false;
	bool started = false;
	for (const char* p = exec; *p != '\0'; p++) {
		if (*p == '"') {
			quoted = !quoted;
			started = true;
		} else if (*p == '\\' && p[1] != '\0') {
			char next = *++p;
			if (quoted && next != '"' && next != '\\' && next != '$'
				&& next != '`')
				return B_BAD_DATA;
			token += next;
			started = true;
		} else if ((*p == ' ' || *p == '\t') && !quoted) {
			if (started) {
				tokens.push_back(token);
				token.clear();
				started = false;
			}
		} else {
			token += *p;
			started = true;
		}
	}
	if (quoted)
		return B_BAD_DATA;
	if (started)
		tokens.push_back(token);
	return tokens.empty() ? B_BAD_DATA : B_OK;
}


static status_t
file_argument(const entry_ref& ref, bool uri, std::string& value)
{
	BPath path;
	status_t status = path.SetTo(&ref);
	if (status != B_OK)
		return status;
	if (!uri) {
		value = path.Path();
		return B_OK;
	}

	value = "file://";
	for (const unsigned char* p = (const unsigned char*)path.Path(); *p; p++) {
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')
			|| (*p >= '0' && *p <= '9') || strchr("/-._~:", *p) != NULL) {
			value += (char)*p;
		} else {
			char encoded[4];
			snprintf(encoded, sizeof(encoded), "%%%02X", *p);
			value += encoded;
		}
	}
	return B_OK;
}


status_t
LaunchDesktopEntry(const DesktopEntry& entry, const entry_ref* refs,
	int32 refCount)
{
	if (!entry.IsValid() || entry.Type() != DesktopEntryTypeApplication)
		return B_BAD_DATA;
	if (entry.GetString("Terminal", "false")[0] == 't')
		return B_NOT_SUPPORTED;

	std::vector<std::string> tokens;
	status_t status = tokenize_exec(entry.Exec(), tokens);
	if (status != B_OK)
		return status;

	std::vector<std::string> arguments;
	for (size_t i = 0; i < tokens.size(); i++) {
		const std::string& token = tokens[i];
		if (token == "%i") {
			if (entry.Icon()[0] != '\0') {
				arguments.push_back("--icon");
				arguments.push_back(entry.Icon());
			}
			continue;
		}

		std::string expanded;
		for (size_t j = 0; j < token.size(); j++) {
			if (token[j] != '%') {
				expanded += token[j];
				continue;
			}
			if (++j >= token.size())
				return B_BAD_DATA;
			switch (token[j]) {
				case '%':
					expanded += '%';
					break;
				case 'c':
					expanded += entry.Name();
					break;
				case 'k':
					expanded += entry.Path();
					break;
				case 'f':
				case 'u':
				{
					if (refCount > 0 && refs != NULL) {
						std::string value;
						status = file_argument(refs[0], token[j] == 'u',
							value);
						if (status != B_OK)
							return status;
						expanded += value;
					}
					break;
				}
				case 'F':
				case 'U':
					if (token.size() != 2)
						return B_BAD_DATA;
					for (int32 k = 0; k < refCount && refs != NULL; k++) {
						std::string value;
						status = file_argument(refs[k], token[j] == 'U',
							value);
						if (status != B_OK)
							return status;
						arguments.push_back(value);
					}
					break;
				case 'd':
				case 'D':
				case 'n':
				case 'N':
				case 'v':
				case 'm':
					break;
				default:
					return B_BAD_DATA;
			}
		}
		if (!expanded.empty())
			arguments.push_back(expanded);
	}
	if (arguments.empty() || arguments[0].empty())
		return B_BAD_DATA;

	std::vector<char*> argv;
	argv.reserve(arguments.size() + 1);
	for (size_t i = 0; i < arguments.size(); i++)
		argv.push_back(const_cast<char*>(arguments[i].c_str()));
	argv.push_back(NULL);

	pid_t child;
	int error = posix_spawnp(&child, argv[0], NULL, NULL, argv.data(), environ);
	return error == 0 ? B_OK : -error;
}
