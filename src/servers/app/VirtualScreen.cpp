/*
 * Copyright 2005-2013, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de

 * Part of Koan, a VitruvianOS fork */


#include "VirtualScreen.h"

#include "HWInterface.h"
#include "Desktop.h"
#include "ScreenScaleTransform.h"

#include <math.h>
#include <new>


VirtualScreen::VirtualScreen()
	:
	fScreenList(4),
	fDrawingEngine(NULL),
	fHWInterface(NULL)
{
}


VirtualScreen::~VirtualScreen()
{
	_Reset();
}


void
VirtualScreen::_Reset()
{
	ScreenList list;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		list.AddItem(item->screen);
	}

	gScreenManager->ReleaseScreens(list);
	fScreenList.MakeEmpty();

	fFrame.Set(0, 0, 0, 0);
	fDrawingEngine = NULL;
	fHWInterface = NULL;
}


status_t
VirtualScreen::SetConfiguration(Desktop& desktop, const ScreenConfigurations& configurations,
	ScreenConfigurations& currentConfigurations, uint32* _changedScreens)
{
	// Remember previous screen modes

	typedef std::map<Screen*, display_mode> ScreenModeMap;
	ScreenModeMap previousModes;
	typedef std::map<Screen*, float> ScreenScaleMap;
	ScreenScaleMap previousScales;
	bool previousModesFailed = false;

	if (_changedScreens != NULL) {
		*_changedScreens = 0;

		try {
			for (int32 i = 0; i < fScreenList.CountItems(); i++) {
				Screen* screen = fScreenList.ItemAt(i)->screen;

				display_mode mode;
				screen->GetMode(mode);

				previousModes.insert(std::make_pair(screen, mode));
				previousScales.insert(std::make_pair(screen,
					screen->Scale()));
			}
		} catch (...) {
			previousModesFailed = true;
			*_changedScreens = ~0L;
		}
	}

	_Reset();

	ScreenList list;
	status_t status = gScreenManager->AcquireScreens(&desktop, NULL, 0,
		desktop.TargetScreen(), false, list);
	if (status != B_OK) {
		// TODO: we would try again here with force == true
		return status;
	}

	for (int32 i = 0; i < list.CountItems(); i++) {
		Screen* screen = list.ItemAt(i);

		AddScreen(screen, configurations, currentConfigurations);

		if (!previousModesFailed && _changedScreens != NULL) {
			// Figure out which screens have changed their mode
			display_mode mode;
			screen->GetMode(mode);

			ScreenModeMap::const_iterator found = previousModes.find(screen);
			if (found != previousModes.end()
				&& memcmp(&mode, &found->second, sizeof(display_mode)))
				*_changedScreens |= 1 << i;

			ScreenScaleMap::const_iterator oldScale
				= previousScales.find(screen);
			if (oldScale != previousScales.end()
				&& screen->Scale() != oldScale->second)
				*_changedScreens |= 1 << i;
		}
	}

	UpdateFrame();
	return B_OK;
}


status_t
VirtualScreen::AddScreen(Screen* screen, const ScreenConfigurations& configurations,
	ScreenConfigurations& currentConfigurations)
{
	screen_item* item = new(std::nothrow) screen_item;
	if (item == NULL)
		return B_NO_MEMORY;

	item->screen = screen;

	status_t status = B_ERROR;
	display_mode mode;
	if (_GetMode(screen, configurations, mode) == B_OK) {
		// we found settings for this screen, and try to apply them now
		status = screen->SetMode(mode);
	}

	if (status != B_OK) {
		// We found no configuration or it wasn't valid, try to fallback to
		// sane values
		status = screen->SetPreferredMode();
		if (status == B_OK) {
			screen->GetMode(mode);
		}
		if (status != B_OK)
			status = screen->SetBestMode(1024, 768, B_RGB32, 60.f);
		if (status != B_OK)
			status = screen->SetBestMode(800, 600, B_RGB32, 60.f, false);
		if (status != B_OK) {
			debug_printf("app_server: Failed to set mode: %s\n",
				strerror(status));

			delete item;
			return status;
		}
	}

	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;
	screen_configuration* configuration = configurations.BestFit(
		screen->ID(), hasInfo ? &info : NULL);
	float scale = configuration != NULL ? configuration->scale : 1.0f;
	status = screen->SetScale(scale);
	if (status != B_OK) {
		delete item;
		return status;
	}
	scale = screen->Scale();

	currentConfigurations.Set(screen->ID(), hasInfo ? &info : NULL, screen->Frame(), mode);
	currentConfigurations.SetScale(screen->ID(), scale);

	// TODO: this works only for single screen configurations
	fDrawingEngine = screen->GetDrawingEngine();
	fHWInterface = screen->HWInterface();
	fFrame = screen->Frame();
	item->frame = fFrame;

	fScreenList.AddItem(item);

	return B_OK;
}


status_t
VirtualScreen::RemoveScreen(Screen* screen)
{
	// not implemented yet (config changes when running)
	return B_ERROR;
}


void
VirtualScreen::UpdateFrame()
{
	float virtualWidth = 0.0f;
	float virtualHeight = 0.0f;

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		BRect frame = ScreenLogicalFrameAt(i);

		// TODO: compute virtual size depending on the actual screen position!
		virtualWidth += frame.Width() + 1.0f;
		virtualHeight = max_c(virtualHeight, frame.Height() + 1.0f);
	}

	fFrame.Set(0, 0, ceilf(virtualWidth) - 1.0f,
		ceilf(virtualHeight) - 1.0f);
}


/*!	Returns the smallest frame that spans over all screens
*/
BRect
VirtualScreen::Frame() const
{
	return fFrame;
}


Screen*
VirtualScreen::ScreenAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->screen;

	return NULL;
}


Screen*
VirtualScreen::ScreenByID(int32 id) const
{
	for (int32 i = fScreenList.CountItems(); i-- > 0;) {
		screen_item* item = fScreenList.ItemAt(i);

		if (item->screen->ID() == id || id == B_MAIN_SCREEN_ID.id)
			return item->screen;
	}

	return NULL;
}


int32
VirtualScreen::ScreenIndex(const Screen* screen) const
{
	for (int32 i = fScreenList.CountItems(); i-- > 0;) {
		if (fScreenList.ItemAt(i)->screen == screen)
			return i;
	}

	return -1;
}


void
VirtualScreen::SetScreenFrame(int32 index, BRect frame)
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		item->frame = frame;
}


BRect
VirtualScreen::ScreenFrameAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->frame;

	return BRect(0, 0, 0, 0);
}


BRect
VirtualScreen::ScreenLogicalFrameAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL) {
		return ScreenScaleTransform::ToLogicalFrame(item->frame,
			item->screen->Scale());
	}

	return BRect(0, 0, 0, 0);
}


float
VirtualScreen::ScreenScaleAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->screen->Scale();

	return 1.0f;
}


float
VirtualScreen::ScreenScaleByID(int32 id) const
{
	for (int32 i = fScreenList.CountItems(); i-- > 0;) {
		screen_item* item = fScreenList.ItemAt(i);

		if (item->screen->ID() == id || id == B_MAIN_SCREEN_ID.id)
			return item->screen->Scale();
	}

	return 1.0f;
}


int32
VirtualScreen::CountScreens() const
{
	return fScreenList.CountItems();
}


status_t
VirtualScreen::_GetMode(Screen* screen, const ScreenConfigurations& configurations,
	display_mode& mode) const
{
	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;

	screen_configuration* configuration = configurations.BestFit(screen->ID(),
		hasInfo ? &info : NULL);
	if (configuration == NULL)
		return B_NAME_NOT_FOUND;

	mode = configuration->mode;
	return B_OK;
}
