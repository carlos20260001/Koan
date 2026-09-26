/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef DISPLAY_LAYOUT_H
#define DISPLAY_LAYOUT_H


#include <vector>

#include <Accelerant.h>
#include <Message.h>
#include <Rect.h>
#include <Region.h>
#include <String.h>


class HWInterface;


// One monitor of the desktop: which connector it is on, what the EDID says
// about it, and where it sits in the frame buffer. Frames are in frame buffer
// pixels, which is what every window and the cursor are measured in; a
// monitor with a scale above 100 percent shows its frame enlarged.
struct DisplayInfo {
	uint32			id;
	BString			name;			// connector, e.g. "DP-2"
	BString			key;			// identifies the monitor across boots

	BString			monitorName;	// from the EDID, may be empty
	BString			vendor;			// PNP id, three letters
	BString			serial;
	uint32			productID;
	uint16			week;
	uint16			year;
	float			widthCM;
	float			heightCM;

	uint32			flags;			// B_DISPLAY_OUTPUT_*
	bool			primary;
	BRect			frame;
	uint16			scale;			// percent
	uint16			renderScale;	// frame buffer pixels per logical pixel,
									// in percent
	display_timing	native;
	display_timing	timing;
	bool			hasEDID;
	bool			pinned;			// placed by the current request; stays
									// put when overlaps are resolved

	bool			IsConnected() const
						{ return (flags & B_DISPLAY_OUTPUT_CONNECTED) != 0; }
	bool			IsEnabled() const
						{ return (flags & B_DISPLAY_OUTPUT_ENABLED) != 0; }
	float			RefreshRate() const;
	float			NativeRefreshRate() const;
	float			DPI() const;
};


/*!	The monitors of a screen and their arrangement.

	The layout is kept by the Desktop and lives in two places: the accelerant,
	which drives the heads accordingly, and the desktop settings, which
	remember what the user chose for each monitor so that it comes back the
	same way after a reboot or when the monitor is plugged in again.
*/
class DisplayLayout {
public:
								DisplayLayout();
								~DisplayLayout();

			// what the hardware has
			status_t			ReadOutputs(HWInterface* interface);
			void				SetSingle(BRect frame, uint16 scale,
									const monitor_info* info);

			// what to do with it
			void				Configure(const BMessage& saved,
									bool keepCurrent);
			status_t			ApplyRequest(const BMessage& request);
			void				GetConfigs(
									std::vector<display_output_config>& configs)
									const;

			// exchanging it
			status_t			Store(BMessage& saved) const;
			status_t			Archive(BMessage& into,
									HWInterface* interface) const;

			int32				CountDisplays() const
									{ return fDisplays.size(); }
			const DisplayInfo*	DisplayAt(int32 index) const;
			const DisplayInfo*	DisplayByID(uint32 id) const;
			const DisplayInfo*	PrimaryDisplay() const;
			const DisplayInfo*	DisplayFor(BRect frame) const;
			const DisplayInfo*	DisplayNearest(BPoint point) const;

			BRect				Frame() const;
			BRegion				Region() const;
			bool				IsEmpty() const { return fDisplays.empty(); }
			bool				HasScaledDisplay() const;
			uint16				RenderScale() const;
									// the density everything is drawn at, in
									// percent: the smallest display scale,
									// so that no display is ever shrunk
			void				SetRenderScale(uint16 renderScale);

	static	uint16				DefaultScale(const DisplayInfo& display);
			uint16				SavedScale(const BMessage& saved) const;
									// what the settings hold for the single
									// display, or its default
	static	BString				MakeKey(const DisplayInfo& display);
	static	bool				IsValidScale(uint16 scale);

private:
			void				_Normalize();
			void				_Separate();
			DisplayInfo*		_DisplayByID(uint32 id);
			const BMessage*		_FindSaved(const BMessage& saved,
									const DisplayInfo& display,
									BMessage& found) const;

			std::vector<DisplayInfo> fDisplays;
			uint16				fRenderScaleOverride;
};


#endif	// DISPLAY_LAYOUT_H
