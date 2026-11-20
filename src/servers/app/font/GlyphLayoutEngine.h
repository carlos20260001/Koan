/*
 * Copyright 2007-2022, Haiku. All rights reserved.
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Dario Casalinuovo
 */

#ifndef GLYPH_LAYOUT_ENGINE_H
#define GLYPH_LAYOUT_ENGINE_H

#include "utf8_functions.h"

#include <EmojiTables.h>
#include "FontCache.h"
#include "FontCacheEntry.h"
#include "GlobalFontManager.h"
#include "ServerFont.h"

#include <Autolock.h>
#include <Debug.h>
#include <ObjectList.h>
#include <SupportDefs.h>

#include <ctype.h>

class FontCacheReference {
public:
	FontCacheReference()
		:
		fCacheEntry(NULL),
		fFallbackReference(NULL),
		fLocked(false),
		fWriteLocked(false)
	{
	}

	~FontCacheReference()
	{
		if (fCacheEntry == NULL)
			return;

		fFallbackReference = NULL;
		Unlock();
		if (fCacheEntry != NULL)
			FontCache::Default()->Recycle(fCacheEntry);
	}

	void SetTo(FontCacheEntry* entry)
	{
		ASSERT(entry != NULL);
		ASSERT(fCacheEntry == NULL);

		fCacheEntry = entry;
	}

	bool ReadLock()
	{
		ASSERT(fCacheEntry != NULL);
		ASSERT(fWriteLocked == false);

		if (fLocked)
			return true;

		if (!fCacheEntry->ReadLock()) {
			_Cleanup();
			return false;
		}

		fLocked = true;
		return true;
	}

	bool WriteLock()
	{
		ASSERT(fCacheEntry != NULL);

		if (fWriteLocked)
			return true;

		if (fLocked) {
			if (!fCacheEntry->ReadUnlock()) {
				_Cleanup();
				return false;
			}
		}
		if (!fCacheEntry->WriteLock()) {
			_Cleanup();
			return false;
		}

		fLocked = true;
		fWriteLocked = true;
		return true;
	}

	bool Unlock()
	{
		ASSERT(fCacheEntry != NULL);

		if (!fLocked)
			return true;

		if (fWriteLocked) {
			if (!fCacheEntry->WriteUnlock()) {
				_Cleanup();
				return false;
			}
		} else {
			if (!fCacheEntry->ReadUnlock()) {
				_Cleanup();
				return false;
			}
		}

		fLocked = false;
		fWriteLocked = false;
		return true;
	}

	bool SetFallback(FontCacheReference* fallback)
	{
		ASSERT(fCacheEntry != NULL);
		ASSERT(fallback != NULL);
		ASSERT(fallback->Entry() != NULL);
		ASSERT(fallback->Entry() != fCacheEntry);

		if (fFallbackReference == fallback)
			return true;

		if (fFallbackReference != NULL) {
			fFallbackReference->Unlock();
			fFallbackReference = NULL;
		}

		// We need to create new glyphs with the engine of the fallback font
		// and store them in the main font cache (not just transfer them from
		// one cache to the other). So we need both to be write-locked.
		if (fallback->Entry() < fCacheEntry) {
			if (fLocked && !Unlock())
				return false;
			if (!fallback->WriteLock()) {
				WriteLock();
				return false;
			}
			fFallbackReference = fallback;
			return WriteLock();
		}
		if (fLocked && !fWriteLocked && !Unlock())
			return false;
		if (!WriteLock() || !fallback->WriteLock())
			return false;
		fFallbackReference = fallback;
		return true;
	}

	inline FontCacheEntry* Entry() const
	{
		return fCacheEntry;
	}

	inline bool WriteLocked() const
	{
		return fWriteLocked;
	}

private:

	void _Cleanup()
	{
		if (fFallbackReference != NULL) {
			fFallbackReference->Unlock();
			fFallbackReference = NULL;
		}
		if (fCacheEntry != NULL)
			FontCache::Default()->Recycle(fCacheEntry);
		fCacheEntry = NULL;
		fLocked = false;
		fWriteLocked = false;
	}

private:
			FontCacheEntry*		fCacheEntry;
			FontCacheReference*	fFallbackReference;
			bool				fLocked;
			bool				fWriteLocked;
};


// The characters an emoji glyph joins have no glyphs of their own
static const GlyphCache sZeroWidthGlyph(0, 0, glyph_data_invalid,
	agg::rect_i(0, 0, -1, -1), 0, 0, 0, 0, 0, 0);


class GlyphLayoutEngine {
public:
	static	bool				IsWhiteSpace(uint32 glyphCode);

	static	FontCacheEntry*		FontCacheEntryFor(const ServerFont& font,
									bool forceVector);

			template<class GlyphConsumer>
	static	bool				LayoutGlyphs(GlyphConsumer& consumer,
									const ServerFont& font,
									const char* utf8String,
									int32 length, int32 maxChars,
									const escapement_delta* delta = NULL,
									uint8 spacing = B_BITMAP_SPACING,
									const BPoint* offsets = NULL,
									FontCacheReference* cacheReference = NULL);

	static	void				PopulateFallbacks(
									BObjectList<FontCacheReference, true>& fallbacks,
									const ServerFont& font, bool forceVector);

	static FontCacheReference*	GetFallbackReference(
									BObjectList<FontCacheReference, true>& fallbacks,
									uint32 charCode);

private:
	static	const GlyphCache*	_CreateGlyph(
									FontCacheReference& cacheReference,
									BObjectList<FontCacheReference, true>& fallbacks,
									const ServerFont& font, bool needsVector,
									uint32 glyphCode);

	static	int32				_GatherEmojiCluster(uint32 firstCode,
									const char* string, const char* start,
									int32 length, int32 maxChars,
									uint32* codes, const char** ends);
	static	bool				_WantsEmojiPresentation(const uint32* codes,
									int32 count);
	static	const GlyphCache*	_CreateEmojiGlyph(
									FontCacheReference& cacheReference,
									BObjectList<FontCacheReference, true>& fallbacks,
									const ServerFont& font, bool needsVector,
									const uint32* codes, int32 count,
									int32& usedCodes);

								GlyphLayoutEngine();
	virtual						~GlyphLayoutEngine();
};


inline bool
GlyphLayoutEngine::IsWhiteSpace(uint32 charCode)
{
	switch (charCode) {
		case 0x0009:	/* tab */
		case 0x000b:	/* vertical tab */
		case 0x000c:	/* form feed */
		case 0x0020:	/* space */
		case 0x00a0:	/* non breaking space */
		case 0x000a:	/* line feed */
		case 0x000d:	/* carriage return */
		case 0x2028:	/* line separator */
		case 0x2029:	/* paragraph separator */
			return true;
	}

	return false;
}


inline FontCacheEntry*
GlyphLayoutEngine::FontCacheEntryFor(const ServerFont& font, bool forceVector)
{
	FontCache* cache = FontCache::Default();
	FontCacheEntry* entry = cache->FontCacheEntryFor(font, forceVector);
	return entry;
}


template<class GlyphConsumer>
inline bool
GlyphLayoutEngine::LayoutGlyphs(GlyphConsumer& consumer,
	const ServerFont& font,
	const char* utf8String, int32 length, int32 maxChars,
	const escapement_delta* delta, uint8 spacing,
	const BPoint* offsets, FontCacheReference* _cacheReference)
{
	// TODO: implement spacing modes
	FontCacheEntry* entry = NULL;
	FontCacheReference* pCacheReference;
	FontCacheReference cacheReference;
	BObjectList<FontCacheReference, true> fallbacksList(21);

	if (_cacheReference != NULL) {
		pCacheReference = _cacheReference;
		entry = _cacheReference->Entry();
		// When there is already a cacheReference, it means there was already
		// an iteration over the glyphs. The use-case is for example to do
		// a layout pass to get the string width for the bounding box, then a
		// second layout pass to actually render the glyphs to the screen.
		// This means that the fallback entry mechanism will not do any good
		// for the second pass, since the fallback glyphs have been stored in
		// the original entry.
	} else
		pCacheReference = &cacheReference;

	if (entry == NULL) {
		entry = FontCacheEntryFor(font, consumer.NeedsVector());

		if (entry == NULL)
			return false;
		pCacheReference->SetTo(entry);
		if (!pCacheReference->ReadLock())
			return false;
	} // else the entry was already used and is still locked

	consumer.Start();

	double x = 0.0;
	double y = 0.0;
	if (offsets) {
		x = offsets[0].x;
		y = offsets[0].y;
	}

	double advanceX = 0.0;
	double advanceY = 0.0;
	double size = font.Size();

	uint32 lastCharCode = 0; // Needed for kerning in B_STRING_SPACING mode
	uint32 charCode;
	int32 index = 0;
	const char* start = utf8String;
	while (maxChars-- > 0 && (charCode = UTF8ToCharCode(&utf8String)) != 0) {

		if (offsets != NULL) {
			// Use direct glyph locations instead of calculating them
			// from the advance values
			x = offsets[index].x;
			y = offsets[index].y;
		} else {
			if (spacing == B_STRING_SPACING)
				entry->GetKerning(lastCharCode, charCode, &advanceX, &advanceY);

			x += advanceX;
			y += advanceY;
		}

		// An emoji, or a sequence of them, may be drawn by a color font
		// as one glyph that the following characters join.
		const GlyphCache* glyph = NULL;
		uint32 clusterCodes[EmojiSequence::kMaxCodes];
		const char* clusterEnds[EmojiSequence::kMaxCodes];
		int32 clusterCount = 0;
		bool emojiStart = charCode >= '#' && emoji_can_start_cluster(charCode);
		if (emojiStart && emoji_is_keycap_base(charCode)) {
			// a digit only begins a keycap: U+FE0F or U+20E3 follows
			emojiStart = utf8String - start < length
				&& ((uint8)*utf8String == 0xef || (uint8)*utf8String == 0xe2);
		}
		if (emojiStart) {
			clusterCount = _GatherEmojiCluster(charCode, utf8String, start,
				length, maxChars, clusterCodes, clusterEnds);
			if (_WantsEmojiPresentation(clusterCodes, clusterCount)) {
				// the font joins the sequence without variation selectors
				uint32 codes[EmojiSequence::kMaxCodes];
				int32 positions[EmojiSequence::kMaxCodes];
				int32 count = 0;
				for (int32 i = 0; i < clusterCount; i++) {
					if (clusterCodes[i] == kVariationSelectorEmoji
						|| clusterCodes[i] == kVariationSelectorText) {
						continue;
					}
					positions[count] = i;
					codes[count++] = clusterCodes[i];
				}

				int32 usedCodes = 0;
				glyph = _CreateEmojiGlyph(*pCacheReference, fallbacksList,
					font, consumer.NeedsVector(), codes, count, usedCodes);
				if (pCacheReference->Entry() == NULL)
					return false;

				if (glyph != NULL) {
					// the characters of the glyph, and a variation selector
					// right after them
					int32 used = positions[usedCodes - 1] + 1;
					if (used < clusterCount
						&& (clusterCodes[used] == kVariationSelectorEmoji
							|| clusterCodes[used] == kVariationSelectorText)) {
						used++;
					}
					clusterCount = used;
				}
			}
			if (glyph == NULL)
				clusterCount = 0;
		}

		if (glyph == NULL)
			glyph = entry->CachedGlyph(charCode);
		if (glyph == NULL) {
			glyph = _CreateGlyph(*pCacheReference, fallbacksList, font,
				consumer.NeedsVector(), charCode);

			// Something may have gone wrong while reacquiring the entry lock
			if (pCacheReference->Entry() == NULL)
				return false;
		}

		if (glyph == NULL) {
			consumer.ConsumeEmptyGlyph(index++, charCode, x, y);
			advanceX = 0;
			advanceY = 0;
		} else {
			// get next increment for pen position
			if (spacing == B_CHAR_SPACING) {
				advanceX = glyph->precise_advance_x * size;
				advanceY = glyph->precise_advance_y * size;
			} else {
				advanceX = glyph->advance_x;
				advanceY = glyph->advance_y;
			}

			// adjust for custom spacing
			if (delta != NULL) {
				advanceX += IsWhiteSpace(charCode)
					? delta->space : delta->nonspace;
			}

			if (!consumer.ConsumeGlyph(index++, charCode, glyph, entry, x, y,
					advanceX, advanceY)) {
				advanceX = 0.0;
				advanceY = 0.0;
				break;
			}

			// The rest of an emoji cluster has no glyphs of its own
			bool stopped = false;
			for (int32 i = 1; i < clusterCount && !stopped; i++) {
				stopped = !consumer.ConsumeGlyph(index++, clusterCodes[i],
					&sZeroWidthGlyph, entry, x, y, 0, 0);
			}
			if (clusterCount > 1) {
				utf8String = clusterEnds[clusterCount - 1];
				maxChars -= clusterCount - 1;
				charCode = clusterCodes[clusterCount - 1];
			}
			if (stopped) {
				advanceX = 0.0;
				advanceY = 0.0;
				break;
			}
		}

		lastCharCode = charCode;
		if (utf8String - start + 1 > length)
			break;
	}

	x += advanceX;
	y += advanceY;
	consumer.Finish(x, y);

	return true;
}


inline const GlyphCache*
GlyphLayoutEngine::_CreateGlyph(FontCacheReference& cacheReference,
	BObjectList<FontCacheReference, true>& fallbacks,
	const ServerFont& font, bool forceVector, uint32 charCode)
{
	FontCacheEntry* entry = cacheReference.Entry();

	// Avoid loading the fallbacks if our font can create the glyph.
	if (entry->CanCreateGlyph(charCode)) {
		if (cacheReference.WriteLock())
			return entry->CreateGlyph(charCode);
		return NULL;
	}

	if (fallbacks.IsEmpty())
		PopulateFallbacks(fallbacks, font, forceVector);

	FontCacheReference* fallbackReference = GetFallbackReference(fallbacks, charCode);
	if (fallbackReference != NULL) {
		if (cacheReference.SetFallback(fallbackReference))
			return entry->CreateGlyph(charCode, fallbackReference->Entry());
		if (cacheReference.Entry() == NULL)
			return NULL;
	}

	// No one knows how to draw this, so use the missing glyph symbol.
	if (cacheReference.WriteLock())
		return entry->CreateGlyph(charCode);
	return NULL;
}


/*!	Collects the characters of the emoji cluster that \a firstCode begins,
	from \a string on (see emoji_cluster_length()). Returns the number of
	characters, at least 1, and the end of each one in \a ends.
*/
inline int32
GlyphLayoutEngine::_GatherEmojiCluster(uint32 firstCode, const char* string,
	const char* start, int32 length, int32 maxChars, uint32* codes,
	const char** ends)
{
	codes[0] = firstCode;
	ends[0] = string;
	int32 count = 1;
	while (count < EmojiSequence::kMaxCodes && count <= maxChars
		&& string - start < length) {
		uint32 code = UTF8ToCharCode(&string);
		if (code == 0)
			break;
		codes[count] = code;
		ends[count] = string;
		count++;
	}

	return emoji_cluster_length(codes, count);
}


/*!	Whether a cluster from _GatherEmojiCluster() is to be shown as an emoji
	rather than as text (UTS #51).
*/
inline bool
GlyphLayoutEngine::_WantsEmojiPresentation(const uint32* codes, int32 count)
{
	if (count > 1) {
		if (codes[1] == kVariationSelectorText)
			return false;
		if (codes[1] == kVariationSelectorEmoji)
			return true;
		for (int32 i = 1; i < count; i++) {
			if (codes[i] == kZeroWidthJoiner || codes[i] == kCombiningKeycap
				|| emoji_is_modifier(codes[i])
				|| emoji_is_regional_indicator(codes[i])
				|| emoji_is_tag(codes[i])) {
				return true;
			}
		}
	}
	if (emoji_is_keycap_base(codes[0]))
		return false;
	return emoji_has_emoji_presentation(codes[0]);
}


/*!	Returns the glyph of a color font for the emoji sequence \a codes (with
	no variation selectors), or NULL when there is no color font with a
	glyph for its first code. The glyph may be for a part of the sequence:
	\a usedCodes returns the number of codes it is for.
*/
inline const GlyphCache*
GlyphLayoutEngine::_CreateEmojiGlyph(FontCacheReference& cacheReference,
	BObjectList<FontCacheReference, true>& fallbacks,
	const ServerFont& font, bool forceVector, const uint32* codes,
	int32 count, int32& usedCodes)
{
	FontCacheEntry* entry = cacheReference.Entry();

	const EmojiSequence* sequence = entry->CachedSequence(codes, count);
	if (sequence != NULL) {
		if (sequence->glyph_key == 0)
			return NULL;
		const GlyphCache* glyph = entry->CachedGlyph(sequence->glyph_key);
		if (glyph != NULL) {
			usedCodes = sequence->used_codes;
			return glyph;
		}
	}

	// The color font: this one, or the first fallback that is one
	FontCacheReference* colorReference = NULL;
	if (!entry->HasColorGlyphs()) {
		if (fallbacks.IsEmpty())
			PopulateFallbacks(fallbacks, font, forceVector);

		for (int32 i = 0; i < fallbacks.CountItems(); i++) {
			FontCacheEntry* fallbackEntry = fallbacks.ItemAt(i)->Entry();
			if (fallbackEntry != NULL && fallbackEntry->HasColorGlyphs()) {
				colorReference = fallbacks.ItemAt(i);
				break;
			}
		}

		if (colorReference == NULL) {
			// remember that there is none
			if (cacheReference.WriteLock())
				entry->CacheSequence(codes, count, 0, 0);
			return NULL;
		}
		if (!cacheReference.SetFallback(colorReference))
			return NULL;
	} else if (!cacheReference.WriteLock())
		return NULL;

	FontCacheEntry* colorEntry = colorReference != NULL
		? colorReference->Entry() : entry;
	uint32 glyphIndex;
	int32 used;
	const GlyphCache* glyph = NULL;
	if (colorEntry->ShapeSequence(codes, count, glyphIndex, used)) {
		glyph = entry->CreateColorGlyph(glyphIndex,
			colorReference != NULL ? colorEntry : NULL);
	}
	if (glyph == NULL || glyph->data_type != glyph_data_color) {
		// a glyph without color is the text presentation's to draw
		entry->CacheSequence(codes, count, 0, 0);
		return NULL;
	}

	entry->CacheSequence(codes, count, kColorGlyphKey | glyphIndex, used);
	usedCodes = used;
	return glyph;
}


inline void
GlyphLayoutEngine::PopulateFallbacks(
	BObjectList<FontCacheReference, true>& fallbacksList,
	const ServerFont& font, bool forceVector)
{
	ASSERT(fallbacksList.IsEmpty());

	// TODO: We always get the fallback glyphs from the Noto family, but of
	// course the fallback font should a) contain the missing glyphs at all
	// and b) be similar to the original font. So there should be a mapping
	// of some kind to know the most suitable fallback font.
	static const char* fallbacks[] = {
		"Noto Sans",
		"Noto Sans Thai",
		"Noto Sans CJK JP",
		// Living scripts from fonts-noto-core, most widely used first.
		"Noto Sans Arabic",
		"Noto Sans Hebrew",
		"Noto Sans Devanagari",
		"Noto Sans Bengali",
		"Noto Sans Tamil",
		"Noto Sans Telugu",
		"Noto Sans Kannada",
		"Noto Sans Malayalam",
		"Noto Sans Gujarati",
		"Noto Sans Gurmukhi",
		"Noto Sans Oriya",
		"Noto Sans Sinhala",
		"Noto Sans Armenian",
		"Noto Sans Georgian",
		"Noto Sans Ethiopic",
		"Noto Sans Khmer",
		"Noto Sans Lao",
		"Noto Sans Myanmar",
		"Noto Serif Tibetan",
		"Noto Sans Thaana",
		"Noto Sans Syriac",
		"Noto Sans Mongolian",
		"Noto Sans Javanese",
		"Noto Sans Balinese",
		"Noto Sans Sundanese",
		"Noto Sans Tifinagh",
		"Noto Sans Canadian Aboriginal",
		"Noto Sans NKo",
		"Noto Sans Ol Chiki",
		"Noto Sans Yi",
		"Noto Sans Vai",
		"Noto Sans Adlam",
		"Noto Sans Cherokee",
		"Noto Sans Symbols",
		// Debian's fonts-noto-core spells this "Noto Sans Symbols2" (no
		// space); both spellings listed so this still works on upstream too.
		"Noto Sans Symbols 2",
		"Noto Sans Symbols2",
		"Noto Sans Math",
		"Noto Color Emoji",
		"Twemoji Mozilla",
		"Twemoji",
		"Noto Emoji",
		// Last resort, not a Noto family: covers every glyph
		// kUTF8ControlMap uses so no menu shortcut draws as tofu.
		"DejaVu Sans",
	};

	if (!gFontManager->Lock())
		return;

	static const int nFallbacks = B_COUNT_OF(fallbacks);
	static const int acceptAnyStyle = 2;

	for (int degradeLevel = 0; degradeLevel <= acceptAnyStyle; degradeLevel++) {
		const char* fontStyle;
		if (degradeLevel == 0)
			fontStyle = font.Style();
		else if (degradeLevel == 1)
			fontStyle = "Regular";
		else
			fontStyle = NULL;

		for (int i = 0; i < nFallbacks; i++) {

			FontStyle* fallbackStyle;
			if (degradeLevel != acceptAnyStyle) {
				fallbackStyle = gFontManager->GetStyle(fallbacks[i], fontStyle);
			} else {
				// At this point we'll just take whatever we are given
				fallbackStyle = gFontManager->GetStyleByIndex(fallbacks[i], 0);
			}

			if (fallbackStyle == NULL)
				continue;

			ServerFont fallbackFont(*fallbackStyle, font.Size());

			FontCacheEntry* entry = FontCacheEntryFor(fallbackFont, forceVector);
			if (entry == NULL)
				continue;

			FontCacheReference* cacheReference = new(std::nothrow) FontCacheReference();
			if (cacheReference != NULL) {
				cacheReference->SetTo(entry);
				fallbacksList.AddItem(cacheReference);
			} else
				FontCache::Default()->Recycle(entry);
		}
	}

	gFontManager->Unlock();
}


inline FontCacheReference*
GlyphLayoutEngine::GetFallbackReference(
	BObjectList<FontCacheReference, true>& fallbacks, uint32 charCode)
{
	int32 count = fallbacks.CountItems();
	for (int32 index = 0; index < count; index++) {
		FontCacheReference* fallbackReference = fallbacks.ItemAt(index);
		FontCacheEntry* fallbackEntry = fallbackReference->Entry();
		if (fallbackEntry != NULL && fallbackEntry->CanCreateGlyph(charCode))
			return fallbackReference;
	}
	return NULL;
}


#endif // GLYPH_LAYOUT_ENGINE_H
