/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Trash Implementation (trash-spec Phase 2)
 * Creates trash directories per XDG_DATA_HOME (Phase 1 basedir).
 */

#include <TrashSpec.h>
#include <File.h>
#include <Path.h>
#include <String.h>

#include <dirent.h>
#include <sys/stat.h>

status_t
CreateTrashDirectories(const char* trashPath)
{
	BPath infoPath(trashPath);
	infoPath.Append(TRASH_INFO_DIR);

	BPath filesPath(trashPath);
	filesPath.Append(TRASH_FILES_DIR);

	mkdir(trashPath, 0755);
	mkdir(infoPath.Path(), 0755);
	mkdir(filesPath.Path(), 0755);

	return B_OK;
}
