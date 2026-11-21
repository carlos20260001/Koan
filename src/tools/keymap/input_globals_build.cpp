/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// The host keymap compiler links BKeymap, whose SetToCurrent() asks the
// input_server for the active keymap. There is no input_server on the
// build host: report no current keymap.

#include <stdlib.h>

#include <input_globals.h>


void
_get_key_map(key_map** map, char** keyBuffer, ssize_t* keyBufferSize)
{
	*map = NULL;
	*keyBuffer = NULL;
	*keyBufferSize = 0;
}
