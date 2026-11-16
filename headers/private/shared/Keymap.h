/*
 * Copyright 2004-2010, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval
 *		Axel Dörfler, axeld@pinc-software.de.
 *		Dario Casalinuovo
 */
#ifndef _KEYMAP_H
#define _KEYMAP_H


#include <DataIO.h>
#include <InterfaceDefs.h>
#include <ObjectList.h>
#include <String.h>
#include <StringList.h>


struct xkb_keymap;


class BKeymap {
public:
								BKeymap();
	virtual						~BKeymap();

			status_t			SetTo(const char* path);
			status_t			SetTo(BDataIO& stream);
			status_t			SetToCurrent();
			status_t			SetToDefault();
			void				Unset();

			// Fill from a xkb_keymap for vitruvian native keycodes support
			status_t			PopulateFromXkb(struct xkb_keymap* keymap);
			// Rule names may be NULL for xkbcommon defaults.
			status_t			PopulateFromXkbNames(const char* rules,
									const char* model, const char* layout,
									const char* variant, const char* options);

			const char*			Chars() const { return fChars; }
			uint32				CharsSize() const { return fCharsSize; }

			bool				IsModifierKey(uint32 keyCode) const;
			uint32				Modifier(uint32 keyCode) const;
			uint32				KeyForModifier(uint32 modifier) const;
			uint8				ActiveDeadKey(uint32 keyCode,
									uint32 modifiers) const;
			uint8				DeadKey(uint32 keyCode, uint32 modifiers,
									bool* isEnabled = NULL) const;
			bool				IsDeadSecondKey(uint32 keyCode,
									uint32 modifiers,
									uint8 activeDeadKey) const;
			void				GetChars(uint32 keyCode, uint32 modifiers,
									uint8 activeDeadKey, char** chars,
									int32* numBytes) const;
			status_t			GetModifiedCharacters(const char* in,
									int32 inModifiers, int32 outModifiers,
									BStringList& _outList);

			const key_map&		Map() const { return fKeys; }

			bool				operator==(const BKeymap& other) const;
			bool				operator!=(const BKeymap& other) const;

			BKeymap&			operator=(const BKeymap& other);

protected:
			int32				Offset(uint32 keyCode, uint32 modifiers,
									uint32* _table = NULL) const;
			uint8				DeadKeyIndex(int32 offset) const;

protected:
			char*				fChars;
			key_map				fKeys;
			uint32				fCharsSize;
};


// Maps keymap display name to xkb layout/variant pair and derives rule options
// (shared between keymap preflet and keyboard add-on). Accepts synthetic
// "xkb:<layout>[:<variant>]" ids; returns thread-local scratch for synthetic ids.
void			look_up_xkb_layout(const char* keymapName,
					const char*& layout, const char*& variant);
const char*		look_up_xkb_modifier_options(const key_map& map);

// Distinguishes a real entry from look_up_xkb_layout()'s silent "us" default.
bool			xkb_layout_name_is_known(const char* keymapName);

// First Haiku name for an xkb layout/variant pair, or NULL when none has one.
const char*		xkb_layout_name_for(const char* layout, const char* variant);

// Valid indices are [0, xkb_layout_table_count()); out of range returns NULL.
int32			xkb_layout_table_count();
const char*		xkb_layout_table_name_at(int32 index);


// One selectable entry in the xkb layout catalog: either a shipped Haiku
// keymap (id is the plain name) or an xkb-only layout/variant (id is the
// synthetic "xkb:<layout>[:<variant>]" form). "label" displays; "id" is
// written to input/layout and fed back into look_up_xkb_layout().
struct xkb_catalog_entry {
	BString		id;
	BString		label;
};

// Fills _catalog with every layout/variant xkb offers, parsed from
// xkeyboard-config's rules file. B_ENTRY_NOT_FOUND if missing, B_ERROR if
// it parsed to nothing. Callers must not treat either as an empty but
// usable catalog.
status_t		get_xkb_layout_catalog(
					BObjectList<xkb_catalog_entry, true>& _catalog);


// One selectable keyboard model xkb offers ("id" is the rules "model="
// value, e.g. "pc105"; "label" displays).
struct xkb_model_entry {
	BString		id;
	BString		label;
};

// Fills _catalog with every model in the rules file's "! model" section.
// Same B_ENTRY_NOT_FOUND/B_ERROR contract as get_xkb_layout_catalog().
status_t		get_xkb_model_catalog(
					BObjectList<xkb_model_entry, true>& _catalog);


#endif	// KEYMAP_H
