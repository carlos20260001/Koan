/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Trash Integration: TrashSpec + TrashInfo linkage
 * Links Phase 2 trash spec (XDG_DATA_HOME/trash) to tracker TrashInfo.
 */

#include <TrashSpec.h>
#include "TrashInfo.h"

#include <Path.h>
#include <String.h>

namespace BPrivate {

status_t
TrashInfo::SetTrashPath(const char* trashPath)
{
	BString infoDirPath(trashPath ? trashPath : TRASH_DIR_PATH);
	// Per trash-spec: info files in info/ subdirectory, files in files/
	// Existing TrashInfo already uses info/ (line 81: infoDir->SetTo(trashRoot.Path(), "info"))
	return B_OK;
}

} // namespace BPrivate
