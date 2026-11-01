/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Trash / Recycle Bin (trash-spec reference)
 * Per desktop-entry-spec ecosystem: trash files stored in XDG directories.
 */

#ifndef _TRASH_SPEC_H
#define _TRASH_SPEC_H

#include <String.h>

// Trash spec: files moved to trash directory structure
// Per basedir-spec: XDG_DATA_HOME/trash (typically ~/.local/share/trash)
#define TRASH_DIR_PATH "trash"
#define TRASH_INFO_DIR "info"
#define TRASH_FILES_DIR "files"

#endif
