/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <DesktopEntry.h>
#include <DesktopEntryScanner.h>
#include <MimeAppsParser.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


static bool
make_directory(const char* path)
{
	return mkdir(path, 0700) == 0;
}


static bool
write_file(const char* path, const char* contents)
{
	FILE* file = fopen(path, "w");
	if (file == NULL)
		return false;
	bool success = fputs(contents, file) >= 0;
	return fclose(file) == 0 && success;
}


static bool
remove_file_if_present(const char* path)
{
	return unlink(path) == 0 || errno == ENOENT;
}


static bool
remove_directory_if_present(const char* path)
{
	return rmdir(path) == 0 || errno == ENOENT;
}


int
main()
{
	char rootTemplate[] = "/tmp/vos-xdg-test-XXXXXX";
	char* root = mkdtemp(rootTemplate);
	if (root == NULL) {
		fprintf(stderr, "cannot create XDG test directory\n");
		return 1;
	}

	char user[1024];
	char system[1024];
	char config[1024];
	char userApps[1024];
	char systemApps[1024];
	char userApp[1024];
	char systemApp[1024];
	char userMasked[1024];
	char systemMasked[1024];
	char userMime[1024];
	char systemMime[1024];
	snprintf(user, sizeof(user), "%s/user", root);
	snprintf(system, sizeof(system), "%s/system", root);
	snprintf(config, sizeof(config), "%s/config", root);
	snprintf(userApps, sizeof(userApps), "%s/applications", user);
	snprintf(systemApps, sizeof(systemApps), "%s/applications", system);
	snprintf(userApp, sizeof(userApp), "%s/org.test.Player.desktop",
		userApps);
	snprintf(systemApp, sizeof(systemApp), "%s/org.test.Player.desktop",
		systemApps);
	snprintf(userMasked, sizeof(userMasked), "%s/org.test.Masked.desktop",
		userApps);
	snprintf(systemMasked, sizeof(systemMasked), "%s/org.test.Masked.desktop",
		systemApps);
	snprintf(userMime, sizeof(userMime), "%s/mimeapps.list", config);
	snprintf(systemMime, sizeof(systemMime), "%s/mimeapps.list", system);

	bool setup = make_directory(user) && make_directory(system)
		&& make_directory(config) && make_directory(userApps)
		&& make_directory(systemApps);
	setup = setup && write_file(userApp,
		"[Desktop Entry]\nType=Application\nName=User Player\n"
		"Exec=/bin/true\n");
	setup = setup && write_file(systemApp,
		"[Desktop Entry]\nType=Application\nName=System Player\n"
		"Exec=/bin/true\n");
	setup = setup && write_file(userMasked,
		"[Desktop Entry]\nType=Application\nName=Masked User\n"
		"Exec=/bin/true\nHidden=true\n");
	setup = setup && write_file(systemMasked,
		"[Desktop Entry]\nType=Application\nName=Masked System\n"
		"Exec=/bin/true\n");
	setup = setup && write_file(userMime,
		"[Default Applications]\ntext/plain=org.test.Player.desktop;"
		"org.test.Masked.desktop;\n");
	setup = setup && write_file(systemMime,
		"[Default Applications]\ntext/plain=org.test.Masked.desktop;\n");

	bool environment = setenv("XDG_DATA_HOME", user, 1) == 0
		&& setenv("XDG_DATA_DIRS", system, 1) == 0
		&& setenv("XDG_CONFIG_HOME", config, 1) == 0
		&& setenv("XDG_CONFIG_DIRS", system, 1) == 0;

	bool passed = setup && environment;
	if (passed) {
		DesktopEntryScanner scanner;
		passed = scanner.Scan() == B_OK && scanner.CountEntries() == 1;
		if (passed) {
			DesktopEntry* entry = scanner.EntryAt(0);
			passed = entry != NULL
				&& strcmp(entry->Name(), "User Player") == 0;
		}
	}
	if (passed) {
		MimeAppsParser parser;
		BString desktopPath;
		const char* defaultApp = NULL;
		passed = parser.LoadAssociations() == B_OK
			&& (defaultApp = parser.DefaultAppForMime("text/plain")) != NULL
			&& strcmp(defaultApp, "org.test.Player.desktop") == 0;
		if (passed) {
			passed = parser.FindDesktopEntry("org.test.Player.desktop",
				&desktopPath) == B_OK
				&& strcmp(desktopPath.String(), userApp) == 0;
		}
	}

	bool cleanup = true;
	cleanup &= remove_file_if_present(userApp);
	cleanup &= remove_file_if_present(systemApp);
	cleanup &= remove_file_if_present(userMasked);
	cleanup &= remove_file_if_present(systemMasked);
	cleanup &= remove_file_if_present(userMime);
	cleanup &= remove_file_if_present(systemMime);
	cleanup &= remove_directory_if_present(userApps);
	cleanup &= remove_directory_if_present(systemApps);
	cleanup &= remove_directory_if_present(user);
	cleanup &= remove_directory_if_present(config);
	cleanup &= remove_directory_if_present(system);
	cleanup &= remove_directory_if_present(root);
	if (!passed || !cleanup) {
		fprintf(stderr, "XDG discovery test failed%s%s\n",
			!passed ? " (setup or behavior)" : "",
			!cleanup ? " (temporary-file cleanup)" : "");
		return 1;
	}

	printf("XDG discovery test passed\n");
	return 0;
}
