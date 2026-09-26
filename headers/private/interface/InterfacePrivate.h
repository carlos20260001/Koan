/*
 * Copyright 2007-2009, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stefano Ceccherini <stefano.ceccherini@gmail.com>
 */
#ifndef _INTERFACE_PRIVATE_H
#define _INTERFACE_PRIVATE_H


#include <GraphicsDefs.h>
#include <Rect.h>
#include <SupportDefs.h>


class BMessage;
class BString;


void _init_global_fonts_();
extern "C" status_t _fini_interface_kit_();


namespace BPrivate {

bool		get_mode_parameter(uint32 mode, int32& width, int32& height,
				uint32& colorSpace);
int32		get_bytes_per_row(color_space colorSpace, int32 width);

void		get_workspaces_layout(uint32* _columns, uint32* _rows);
void		set_workspaces_layout(uint32 columns, uint32 rows);

bool		get_control_look(BString& path);
status_t	set_control_look(const BString& path);

void		invalidate_icon_scale();
	// the screen scale changed: BIconUtils::IconScale() asks again

// The monitors making up the screen, their arrangement and scaling. The
// layout message holds one "display" message per monitor; see
// DisplayLayout::Archive() and ApplyRequest() in app_server for the fields.
status_t	get_display_layout(BMessage& layout);
status_t	set_display_layout(const BMessage& request);
status_t	get_display_frame(BRect frame, bool forZoom, BRect& _displayFrame);
				// the frame of the monitor most of \a frame is on; with
				// \a forZoom, the whole screen when zooming to one monitor
				// is turned off
bool		get_zoom_to_display();
void		set_zoom_to_display(bool zoomToDisplay);

}	// namespace BPrivate


#endif	// _INTERFACE_PRIVATE_H
