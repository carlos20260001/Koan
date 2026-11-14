/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOIPOP_COLOR_SCHEME_H
#define _KOIPOP_COLOR_SCHEME_H


/*!	The KoiPop light and dark color schemes as system UI colors.

	The Appearance preferences apply a scheme with set_ui_colors(), so every
	application, the decorators and the control looks pick it up through the
	normal color mechanism. The palette and its contrast checks live in
	KoiPopTheme.h.
*/


#include <Entry.h>
#include <FindDirectory.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Path.h>
#include <String.h>

#include <KoiPopTheme.h>


namespace BPrivate {


/*!	Whether the KoiPop add-on at \a subpath of the system add-ons directory
	(e.g. "control_look/KoiPopControlLook") is installed; its full path goes
	to \a path. With KoiPop installed the system's defaults are KoiPop: the
	decorator, the control look and the light color scheme, each used only
	when the user has not saved a choice of their own.
*/
inline bool
koipop_installed_add_on(const char* subpath, BString* path = NULL)
{
	BPath addOn;
	if (find_directory(B_SYSTEM_ADDONS_DIRECTORY, &addOn) != B_OK
		|| addOn.Append(subpath) != B_OK
		|| !BEntry(addOn.Path()).Exists())
		return false;
	if (path != NULL)
		*path = addOn.Path();
	return true;
}


inline rgb_color
koipop_rgb_color(KoiPop::Color color)
{
	return make_color(color.red, color.green, color.blue, 255);
}


inline KoiPop::Color
koipop_color(rgb_color color)
{
	return KoiPop::MakeColor(color.red, color.green, color.blue, color.alpha);
}


/*!	Adds (or replaces) the UI colors of the KoiPop scheme of \a appearance in
	\a colors, keyed by ui_color_name(). Colors the scheme does not define,
	such as the desktop color, are left alone.
*/
inline void
koipop_get_color_scheme(KoiPop::Appearance appearance, BMessage& colors)
{
	const KoiPop::Palette& p = KoiPop::GetPalette(appearance);
	const bool dark = appearance == KoiPop::kDarkAppearance;

	struct Entry {
		color_which		which;
		KoiPop::Color	color;
	};
	const Entry entries[] = {
		{ B_PANEL_BACKGROUND_COLOR, p.windowBackground },
		{ B_PANEL_TEXT_COLOR, p.windowText },
		{ B_DOCUMENT_BACKGROUND_COLOR, p.documentBackground },
		{ B_DOCUMENT_TEXT_COLOR, p.documentText },
		{ B_CONTROL_BACKGROUND_COLOR, p.controlBackground },
		{ B_CONTROL_TEXT_COLOR, p.controlText },
		{ B_CONTROL_BORDER_COLOR, p.controlBorder },
		{ B_CONTROL_HIGHLIGHT_COLOR, p.ocean },
		{ B_CONTROL_MARK_COLOR, dark ? p.selectionMark : p.oceanDeep },
		{ B_NAVIGATION_BASE_COLOR, p.focusRing },
		{ B_NAVIGATION_PULSE_COLOR, p.turquoise },
		{ B_SHINE_COLOR, KoiPop::MakeColor(255, 255, 255) },
		{ B_SHADOW_COLOR, KoiPop::MakeColor(0, 0, 0) },
		{ B_LINK_TEXT_COLOR, p.linkText },
		{ B_LINK_HOVER_COLOR, p.linkText },
		{ B_LINK_VISITED_COLOR, p.linkVisitedText },
		{ B_LINK_ACTIVE_COLOR, p.linkText },
		{ B_MENU_BACKGROUND_COLOR, p.menuBackground },
		{ B_MENU_SELECTED_BACKGROUND_COLOR, p.menuSelectedBackground },
		{ B_MENU_ITEM_TEXT_COLOR, p.menuText },
		{ B_MENU_SELECTED_ITEM_TEXT_COLOR, p.menuSelectedText },
		{ B_MENU_SELECTED_BORDER_COLOR, p.oceanDeep },
		{ B_LIST_BACKGROUND_COLOR, p.listBackground },
		{ B_LIST_SELECTED_BACKGROUND_COLOR, p.listSelectedBackground },
		{ B_LIST_ITEM_TEXT_COLOR, p.listText },
		{ B_LIST_SELECTED_ITEM_TEXT_COLOR, p.listSelectedText },
		{ B_SCROLL_BAR_THUMB_COLOR, p.controlBackground },
		{ B_TOOL_TIP_BACKGROUND_COLOR, p.toolTipBackground },
		{ B_TOOL_TIP_TEXT_COLOR, p.toolTipText },
		{ B_STATUS_BAR_COLOR, p.progressBottom },
		{ B_SUCCESS_COLOR, p.success },
		{ B_FAILURE_COLOR, p.destructive },
		// Decorators that paint a tab in the tab color put the window text
		// color on it; the satin title colors are a checked pair.
		{ B_WINDOW_TAB_COLOR, p.titleBottom },
		{ B_WINDOW_TEXT_COLOR, p.titleText },
		{ B_WINDOW_INACTIVE_TAB_COLOR, p.inactiveTitleBottom },
		{ B_WINDOW_INACTIVE_TEXT_COLOR, p.inactiveTitleText },
		{ B_WINDOW_BORDER_COLOR, p.windowBorder },
		{ B_WINDOW_INACTIVE_BORDER_COLOR, p.inactiveWindowBorder }
	};

	for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
		const char* name = ui_color_name(entries[i].which);
		if (name == NULL)
			continue;
		colors.RemoveName(name);
		colors.AddColor(name, koipop_rgb_color(entries[i].color));
	}
}


}	// namespace BPrivate


#endif	// _KOIPOP_COLOR_SCHEME_H
