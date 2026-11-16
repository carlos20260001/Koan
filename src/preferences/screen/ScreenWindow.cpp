/*
 * Copyright 2001-2026 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus, superstippi@gmx.de
 *		Andrew Bachmann
 *		Stefano Ceccherini, burton666@libero.it
 *		Alexandre Deckner, alex@zappotek.com
 *		Axel Dörfler, axeld@pinc-software.de
 *		Rene Gollent, rene@gollent.com
 *		Thomas Kurschel
 *		Rafael Romo
 *		John Scipione, jscipione@gmail.com
 */


#include "ScreenWindow.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <algorithm>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <SeparatorView.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <Spinner.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include <InterfacePrivate.h>

#include "AlertWindow.h"
#include "Constants.h"
#include "DisplayArrangementView.h"
#include "IdentifyWindow.h"
#include "RefreshWindow.h"
#include "ScreenSettings.h"
#include "Utility.h"

/* Note, this headers defines a *private* interface to the Radeon accelerant.
 * It's a solution that works with the current BeOS interface that Haiku
 * adopted.
 * However, it's not a nice and clean solution. Don't use this header in any
 * application if you can avoid it. No other driver is using this, or should
 * be using this.
 * It will be replaced as soon as we introduce an updated accelerant interface
 * which may even happen before R1 hits the streets.
 */
#include "multimon.h"	// the usual: DANGER WILL, ROBINSON!


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screen"


const char* kBackgroundsSignature = "application/x-vnd.vos-Backgrounds";

static const bigtime_t kReloadDelay = 300000;
	// how long we wait after ScreenChanged() before reading the layout again

// list of officially supported colour spaces
static const struct {
	color_space	space;
	const char*	label;
} kColorSpaces[] = {
	{ B_CMAP8, B_TRANSLATE("8 bits/pixel, 256 colors") },
	{ B_RGB15, B_TRANSLATE("15 bits/pixel, 32768 colors") },
	{ B_RGB16, B_TRANSLATE("16 bits/pixel, 65536 colors") },
	{ B_RGB24, B_TRANSLATE("24 bits/pixel, 16 million colors") },
	{ B_RGB32, B_TRANSLATE("24 bits/pixel, 16 million colors") },
	{ B_RGB30, B_TRANSLATE("30 bits/pixel, 1 billion colors") }
};
static const int32 kColorSpaceCount = B_COUNT_OF(kColorSpaces);

// list of standard refresh rates
static const int32 kRefreshRates[] = { 60, 70, 72, 75, 80, 85, 95, 100 };
static const int32 kRefreshRateCount = B_COUNT_OF(kRefreshRates);

// list of combine modes
static const struct {
	combine_mode	mode;
	const char		*name;
} kCombineModes[] = {
	{ kCombineDisable, B_TRANSLATE("disable") },
	{ kCombineHorizontally, B_TRANSLATE("horizontally") },
	{ kCombineVertically, B_TRANSLATE("vertically") }
};
static const int32 kCombineModeCount = B_COUNT_OF(kCombineModes);

// more unique resolutions than this and the pop-up becomes a matrix
static const int32 kMaxResolutionColumnItems = 16;


static BString
tv_standard_to_string(uint32 mode)
{
	switch (mode) {
		case 0:		return "disabled";
		case 1:		return "NTSC";
		case 2:		return "NTSC Japan";
		case 3:		return "PAL BDGHI";
		case 4:		return "PAL M";
		case 5:		return "PAL N";
		case 6:		return "SECAM";
		case 101:	return "NTSC 443";
		case 102:	return "PAL 60";
		case 103:	return "PAL NC";
		default:
		{
			BString name;
			name << "??? (" << mode << ")";

			return name;
		}
	}
}


static void
resolution_to_string(int32 width, int32 height, BString& string)
{
	string.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32" × %" B_PRId32,
			"The '×' is the Unicode multiplication sign U+00D7"),
			width, height);
}


static void
refresh_rate_to_string(float refresh, BString &string,
	bool appendUnit = true, bool alwaysWithFraction = false)
{
	snprintf(string.LockBuffer(32), 32, "%.*g", refresh >= 100.0 ? 4 : 3,
		refresh);
	string.UnlockBuffer();

	if (appendUnit)
		string << " " << B_TRANSLATE("Hz");
}


static const char*
screen_errors(status_t status)
{
	switch (status) {
		case B_ENTRY_NOT_FOUND:
			return B_TRANSLATE("Unknown mode");
		// TODO: add more?

		default:
			return strerror(status);
	}
}


/*!	Collects the distinct resolutions of \a modes, largest first. */
static void
collect_resolutions(const std::vector<display_mode_entry>& modes,
	std::vector<display_mode_entry>& resolutions)
{
	for (size_t i = 0; i < modes.size(); i++) {
		bool found = false;
		for (size_t j = 0; j < resolutions.size(); j++) {
			if (resolutions[j].width == modes[i].width
				&& resolutions[j].height == modes[i].height) {
				found = true;
				break;
			}
		}
		if (!found)
			resolutions.push_back(modes[i]);
	}

	std::sort(resolutions.begin(), resolutions.end(),
		[](const display_mode_entry& a, const display_mode_entry& b) {
			if (a.width != b.width)
				return a.width > b.width;
			return a.height > b.height;
		});
}


/*!	Collects the distinct refresh rates \a modes offer at the given
	resolution, highest first.
*/
static void
collect_refresh_rates(const std::vector<display_mode_entry>& modes,
	int32 width, int32 height, std::vector<float>& rates)
{
	for (size_t i = 0; i < modes.size(); i++) {
		if (modes[i].width != width || modes[i].height != height
			|| modes[i].refresh <= 0)
			continue;

		bool found = false;
		for (size_t j = 0; j < rates.size(); j++) {
			if (refresh_rates_equal(rates[j], modes[i].refresh)) {
				found = true;
				break;
			}
		}
		if (!found)
			rates.push_back(modes[i].refresh);
	}

	std::sort(rates.begin(), rates.end(), std::greater<float>());
}


static void
add_part(BString& string, const BString& part)
{
	if (part.Length() == 0)
		return;
	if (string.Length() > 0)
		string << " \xc2\xb7 ";
			// middle dot, U+00B7
	string << part;
}


//	#pragma mark - ScreenWindow


ScreenWindow::ScreenWindow(ScreenSettings* settings)
	:
	BWindow(settings->WindowFrame(), B_TRANSLATE_SYSTEM_NAME("Screen"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS,
		B_ALL_WORKSPACES),
	fSettings(settings),
	fIsVesa(false),
	fHasLayout(false),
	fUndoIsScale(false),
	fUndoScaleAndMode(false),
	fBootWorkspaceApplied(false),
	fSelectedID(-1),
	fReloadRunner(NULL),
	fResolutionMatrix(false),
	fOtherRefresh(NULL),
	fSupportedColorSpaces(0),
	fUserSelectedColorSpace(NULL),
	fScreenMode(this),
	fUndoScreenMode(this),
	fModified(false)
{
	BScreen screen(this);

	accelerant_device_info info;
	if (screen.GetDeviceInfo(&info) == B_OK
		&& !strcasecmp(info.chipset, "VESA"))
		fIsVesa = true;

	_UpdateOriginal();
	_BuildSupportedColorSpaces();
	fActive = fSelected = fOriginal;

	_LoadLayout();

	// The arrangement of the displays, on the left

	fArrangementView = new DisplayArrangementView("arrangement");
	fArrangementView->SetDraggingEnabled(fHasLayout);

	fIdentifyButton = new BButton("identify",
		B_TRANSLATE("Identify displays"),
		new BMessage(kMsgIdentifyDisplays));
	fBackgroundsButton = new BButton("backgrounds",
		B_TRANSLATE("Set background" B_UTF8_ELLIPSIS),
		new BMessage(BUTTON_LAUNCH_BACKGROUNDS_MSG));

	fZoomBox = new BCheckBox("zoom",
		B_TRANSLATE("Maximize windows to the display they are on"),
		new BMessage(kMsgZoomToDisplay));
	fZoomBox->SetToolTip(B_TRANSLATE("When turned off, a maximized window "
		"spans all displays, as it always did."));
	fZoomBox->SetValue(fCurrentLayout.ZoomToDisplay()
		? B_CONTROL_ON : B_CONTROL_OFF);

	fMirrorBox = new BCheckBox("mirror", B_TRANSLATE("Mirror displays"),
		new BMessage(kMsgMirrorDisplays));
	fMirrorBox->SetToolTip(B_TRANSLATE("Every display shows what the main "
		"display shows."));

	fOriginalDarkMode = ui_dark_mode();
	fDarkModeBox = new BCheckBox("dark mode", B_TRANSLATE("Dark mode"),
		new BMessage(kMsgDarkMode));
	fDarkModeBox->SetToolTip(B_TRANSLATE("Windows, menus and controls use "
		"dark colors. Applications and websites that have a dark appearance "
		"use it."));
	fDarkModeBox->SetValue(fOriginalDarkMode ? B_CONTROL_ON : B_CONTROL_OFF);

	// The details of the selected display, on the right

	BView* detailsView = _BuildDetailsPanel();

	// Buttons

	fDefaultsButton = new BButton("DefaultsButton", B_TRANSLATE("Defaults"),
		new BMessage(BUTTON_DEFAULTS_MSG));
	fRevertButton = new BButton("RevertButton", B_TRANSLATE("Revert"),
		new BMessage(BUTTON_REVERT_MSG));
	fRevertButton->SetEnabled(false);
	fApplyButton = new BButton("ApplyButton", B_TRANSLATE("Apply"),
		new BMessage(BUTTON_APPLY_MSG));
	fApplyButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 1.0f)
				.Add(fArrangementView, 1.0f)
				.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
					.Add(fIdentifyButton)
					.Add(fBackgroundsButton)
					.AddGlue()
					.End()
				.Add(fMirrorBox)
				.Add(fZoomBox)
				.Add(fDarkModeBox)
				.End()
			.Add(new BSeparatorView(B_VERTICAL))
			.AddGroup(B_VERTICAL, 0, 0.0f)
				.Add(detailsView)
				.End()
			.End()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fDefaultsButton)
			.Add(fRevertButton)
			.AddGlue()
			.Add(fApplyButton)
			.End();

	_UpdateArrangementView();
	_UpdateDetails();
	if (!fHasLayout)
		_UpdateFallbackControls();
	_CheckApplyEnabled();

	MoveOnScreen();
}


ScreenWindow::~ScreenWindow()
{
	delete fReloadRunner;
	delete fSettings;
}


bool
ScreenWindow::QuitRequested()
{
	fSettings->SetWindowFrame(Frame());

	// Write mode of workspace 0 (the boot workspace) to the vesa settings file
	screen_mode vesaMode;
	if (fBootWorkspaceApplied && fScreenMode.Get(vesaMode, 0) == B_OK) {
		status_t status = _WriteVesaModeFile(vesaMode);
		if (status < B_OK) {
			BString warning = B_TRANSLATE("Could not write VESA mode settings"
				" file:\n\t");
			warning << strerror(status);
			BAlert* alert = new BAlert(B_TRANSLATE("Warning"),
				warning.String(), B_TRANSLATE("OK"), NULL,
				NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
	}

	be_app->PostMessage(B_QUIT_REQUESTED);

	return BWindow::QuitRequested();
}


//	#pragma mark - building the UI


BView*
ScreenWindow::_BuildDetailsPanel()
{
	BScreen screen(this);

	// title and subtitle

	fTitleView = new BStringView("title", "");
	BFont titleFont(be_bold_font);
	titleFont.SetSize(ceilf(be_bold_font->Size() * 1.3f));
	fTitleView->SetFont(&titleFont);

	fSubtitleView = new BStringView("subtitle", "");
	fSubtitleView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_1_TINT);

	// "All workspaces / Current workspace" - only without a display layout

	BPopUpMenu* workspaceMenu = new BPopUpMenu(
		B_TRANSLATE("Current workspace"), true, true);
	fAllWorkspacesItem = new BMenuItem(B_TRANSLATE("All workspaces"),
		new BMessage(WORKSPACE_CHECK_MSG));
	workspaceMenu->AddItem(fAllWorkspacesItem);
	workspaceMenu->AddItem(new BMenuItem(B_TRANSLATE("Current workspace"),
		new BMessage(WORKSPACE_CHECK_MSG)));
	fAllWorkspacesItem->SetMarked(true);

	fWorkspaceField = new BMenuField("WorkspaceMenu",
		B_TRANSLATE("Apply to:"), workspaceMenu);
	fWorkspaceField->SetAlignment(B_ALIGN_RIGHT);
	if (fHasLayout)
		fWorkspaceField->Hide();

	// resolution

	int32 resolutionCount = 0;
	if (fHasLayout) {
		for (int32 i = 0; i < fCurrentLayout.CountDisplays(); i++) {
			std::vector<display_mode_entry> resolutions;
			collect_resolutions(fCurrentLayout.DisplayAt(i)->modes,
				resolutions);
			resolutionCount = std::max(resolutionCount,
				(int32)resolutions.size());
		}
	} else {
		int32 previousWidth = 0;
		int32 previousHeight = 0;
		for (int32 i = 0; i < fScreenMode.CountModes(); i++) {
			screen_mode mode = fScreenMode.ModeAt(i);
			if (mode.width == previousWidth && mode.height == previousHeight)
				continue;
			resolutionCount++;
			previousWidth = mode.width;
			previousHeight = mode.height;
		}
	}
	fResolutionMatrix = resolutionCount > kMaxResolutionColumnItems;

	fResolutionMenu = new BPopUpMenu("resolution", true, true,
		fResolutionMatrix ? B_ITEMS_IN_MATRIX : B_ITEMS_IN_COLUMN);
	fResolutionField = new BMenuField("ResolutionMenu",
		B_TRANSLATE("Resolution:"), fResolutionMenu);
	fResolutionField->SetAlignment(B_ALIGN_RIGHT);

	// refresh rate

	fRefreshMenu = new BPopUpMenu("refresh rate", true, true);
	fRefreshField = new BMenuField("RefreshMenu",
		B_TRANSLATE("Refresh rate:"), fRefreshMenu);
	fRefreshField->SetAlignment(B_ALIGN_RIGHT);
	if (_IsVesa())
		fRefreshField->Hide();

	// colors - a display layout always uses 32 bits

	fColorsMenu = new BPopUpMenu("colors", true, false);
	fColorsField = new BMenuField("ColorsMenu", B_TRANSLATE("Colors:"),
		fColorsMenu);
	fColorsField->SetAlignment(B_ALIGN_RIGHT);
	if (fHasLayout)
		fColorsField->Hide();

	// scale

	fScaleMenu = new BPopUpMenu("scale", true, true);
	const std::vector<int32>& scales = fCurrentLayout.Scales();
	for (size_t i = 0; i < scales.size(); i++) {
		BMessage* message = new BMessage(kMsgScaleChanged);
		message->AddInt32("scale", scales[i]);

		BString label;
		label.SetToFormat("%" B_PRId32 "%%", scales[i]);
		fScaleMenu->AddItem(new BMenuItem(label.String(), message));
	}
	fScaleField = new BMenuField("ScaleMenu", B_TRANSLATE("Scale:"),
		fScaleMenu);
	fScaleField->SetAlignment(B_ALIGN_RIGHT);
	if (!fCurrentLayout.CanScale()) {
		fScaleField->SetEnabled(false);
		fScaleField->SetToolTip(
			B_TRANSLATE("The graphics driver does not support scaling."));
	}

	// Radeon multi-monitor tunnel - only without a display layout, and only
	// when the driver supports it
	{
		bool dummy;
		uint32 dummy32;
		bool multiMonSupport = !fHasLayout
			&& TestMultiMonSupport(&screen) == B_OK;
		bool useLaptopPanelSupport = !fHasLayout
			&& GetUseLaptopPanel(&screen, &dummy) == B_OK;
		bool tvStandardSupport = !fHasLayout
			&& GetTVStandard(&screen, &dummy32) == B_OK;

		// even if there is no support, we still create all controls
		// to make sure we don't access NULL pointers later on

		fCombineMenu = new BPopUpMenu("CombineDisplays", true, true);
		for (int32 i = 0; i < kCombineModeCount; i++) {
			BMessage* message = new BMessage(POP_COMBINE_DISPLAYS_MSG);
			message->AddInt32("mode", kCombineModes[i].mode);
			fCombineMenu->AddItem(new BMenuItem(kCombineModes[i].name,
				message));
		}
		fCombineField = new BMenuField("CombineMenu",
			B_TRANSLATE("Combine displays:"), fCombineMenu);
		fCombineField->SetAlignment(B_ALIGN_RIGHT);
		if (!multiMonSupport)
			fCombineField->Hide();

		fSwapDisplaysMenu = new BPopUpMenu("SwapDisplays", true, true);

		// !order is important - we rely that boolean value == idx
		BMessage* message = new BMessage(POP_SWAP_DISPLAYS_MSG);
		message->AddBool("swap", false);
		fSwapDisplaysMenu->AddItem(new BMenuItem(B_TRANSLATE("no"), message));

		message = new BMessage(POP_SWAP_DISPLAYS_MSG);
		message->AddBool("swap", true);
		fSwapDisplaysMenu->AddItem(new BMenuItem(B_TRANSLATE("yes"),
			message));

		fSwapDisplaysField = new BMenuField("SwapMenu",
			B_TRANSLATE("Swap displays:"), fSwapDisplaysMenu);
		fSwapDisplaysField->SetAlignment(B_ALIGN_RIGHT);
		if (!multiMonSupport)
			fSwapDisplaysField->Hide();

		fUseLaptopPanelMenu = new BPopUpMenu("UseLaptopPanel", true, true);

		// !order is important - we rely that boolean value == idx
		message = new BMessage(POP_USE_LAPTOP_PANEL_MSG);
		message->AddBool("use", false);
		fUseLaptopPanelMenu->AddItem(new BMenuItem(B_TRANSLATE("if needed"),
			message));

		message = new BMessage(POP_USE_LAPTOP_PANEL_MSG);
		message->AddBool("use", true);
		fUseLaptopPanelMenu->AddItem(new BMenuItem(B_TRANSLATE("always"),
			message));

		fUseLaptopPanelField = new BMenuField("UseLaptopPanel",
			B_TRANSLATE("Use laptop panel:"), fUseLaptopPanelMenu);
		fUseLaptopPanelField->SetAlignment(B_ALIGN_RIGHT);
		if (!useLaptopPanelSupport)
			fUseLaptopPanelField->Hide();

		fTVStandardMenu = new BPopUpMenu("TVStandard", true, true);

		// arbitrary limit
		uint32 i = 0;
		if (tvStandardSupport) {
			for (; i < 100; ++i) {
				uint32 mode;
				if (GetNthSupportedTVStandard(&screen, i, &mode) != B_OK)
					break;

				BString name = tv_standard_to_string(mode);

				message = new BMessage(POP_TV_STANDARD_MSG);
				message->AddInt32("tv_standard", mode);

				fTVStandardMenu->AddItem(new BMenuItem(name.String(),
					message));
			}
		}

		fTVStandardField = new BMenuField("tv standard",
			B_TRANSLATE("Video format:"), fTVStandardMenu);
		fTVStandardField->SetAlignment(B_ALIGN_RIGHT);
		if (!tvStandardSupport || i == 0)
			fTVStandardField->Hide();
	}

	if (!fHasLayout)
		_BuildFallbackMenus();

	// enabled / main display

	fEnabledBox = new BCheckBox("enabled", B_TRANSLATE("Enabled"),
		new BMessage(kMsgDisplayEnabled));
	fPrimaryBox = new BCheckBox("primary", B_TRANSLATE("Main display"),
		new BMessage(kMsgDisplayPrimary));
	fPrimaryBox->SetToolTip(
		B_TRANSLATE("The main display is the one with the Deskbar."));

	// read-only information

	fConnectorLabel = new BStringView("connector label",
		B_TRANSLATE("Connector:"));
	fConnectorView = new BStringView("connector", "");
	fSerialLabel = new BStringView("serial label",
		B_TRANSLATE("Serial number:"));
	fSerialView = new BStringView("serial", "");
	fManufacturedLabel = new BStringView("manufactured label",
		B_TRANSLATE("Manufactured:"));
	fManufacturedView = new BStringView("manufactured", "");
	fSizeLabel = new BStringView("size label", B_TRANSLATE("Size:"));
	fSizeView = new BStringView("size", "");
	fDeviceLabel = new BStringView("device label",
		B_TRANSLATE("Graphics card:"));
	fDeviceInfo = new BStringView("device info", "");

	BStringView* labels[] = { fConnectorLabel, fSerialLabel,
		fManufacturedLabel, fSizeLabel, fDeviceLabel };
	for (size_t i = 0; i < B_COUNT_OF(labels); i++)
		labels[i]->SetAlignment(B_ALIGN_RIGHT);

	accelerant_device_info deviceInfo;
	BString deviceString;
	if (fScreenMode.GetDeviceInfo(deviceInfo) == B_OK) {
		if (deviceInfo.name[0] && deviceInfo.chipset[0]) {
			deviceString.SetToFormat("%s (%s)", deviceInfo.name,
				deviceInfo.chipset);
		} else if (deviceInfo.name[0] || deviceInfo.chipset[0]) {
			deviceString
				= deviceInfo.name[0] ? deviceInfo.name : deviceInfo.chipset;
		}
	}
	fDeviceInfo->SetText(deviceString);
	if (deviceString.Length() == 0) {
		fDeviceLabel->Hide();
		fDeviceInfo->Hide();
	}

	// brightness

	fBrightnessSlider = new BSlider("brightness", B_TRANSLATE("Brightness:"),
		NULL, 0, 255, B_HORIZONTAL);
	if (screen.GetBrightness(&fOriginalBrightness) == B_OK) {
		fBrightnessSlider->SetModificationMessage(
			new BMessage(SLIDER_BRIGHTNESS_MSG));
		fBrightnessSlider->SetValue(fOriginalBrightness * 255);
	} else {
		// The driver does not support changing the brightness,
		// so hide the slider
		fBrightnessSlider->Hide();
		fOriginalBrightness = -1;
	}

	// workspaces

	fColumnsControl = new BSpinner("columns", B_TRANSLATE("Columns:"),
		new BMessage(kMsgWorkspaceColumnsChanged));
	fColumnsControl->SetRange(1, 32);
	fRowsControl = new BSpinner("rows", B_TRANSLATE("Rows:"),
		new BMessage(kMsgWorkspaceRowsChanged));
	fRowsControl->SetRange(1, 32);

	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);
	fColumnsControl->SetValue(columns);
	fRowsControl->SetValue(rows);
	_UpdateWorkspaceButtons();

	BStringView* workspacesLabel = new BStringView("workspaces label",
		B_TRANSLATE("Workspaces:"));

	return BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(fTitleView)
		.Add(fSubtitleView)
		.AddStrut(B_USE_SMALL_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(fWorkspaceField->CreateLabelLayoutItem(), 0, 0)
			.Add(fWorkspaceField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(fResolutionField->CreateLabelLayoutItem(), 0, 1)
			.Add(fResolutionField->CreateMenuBarLayoutItem(), 1, 1)
			.Add(fRefreshField->CreateLabelLayoutItem(), 0, 2)
			.Add(fRefreshField->CreateMenuBarLayoutItem(), 1, 2)
			.Add(fColorsField->CreateLabelLayoutItem(), 0, 3)
			.Add(fColorsField->CreateMenuBarLayoutItem(), 1, 3)
			.Add(fScaleField->CreateLabelLayoutItem(), 0, 4)
			.Add(fScaleField->CreateMenuBarLayoutItem(), 1, 4)
			.Add(fCombineField->CreateLabelLayoutItem(), 0, 5)
			.Add(fCombineField->CreateMenuBarLayoutItem(), 1, 5)
			.Add(fSwapDisplaysField->CreateLabelLayoutItem(), 0, 6)
			.Add(fSwapDisplaysField->CreateMenuBarLayoutItem(), 1, 6)
			.Add(fUseLaptopPanelField->CreateLabelLayoutItem(), 0, 7)
			.Add(fUseLaptopPanelField->CreateMenuBarLayoutItem(), 1, 7)
			.Add(fTVStandardField->CreateLabelLayoutItem(), 0, 8)
			.Add(fTVStandardField->CreateMenuBarLayoutItem(), 1, 8)
			.End()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fEnabledBox)
			.Add(fPrimaryBox)
			.AddGlue()
			.End()
		.AddStrut(B_USE_SMALL_SPACING)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(fConnectorLabel, 0, 0)
			.Add(fConnectorView, 1, 0)
			.Add(fSerialLabel, 0, 1)
			.Add(fSerialView, 1, 1)
			.Add(fManufacturedLabel, 0, 2)
			.Add(fManufacturedView, 1, 2)
			.Add(fSizeLabel, 0, 3)
			.Add(fSizeView, 1, 3)
			.Add(fDeviceLabel, 0, 4)
			.Add(fDeviceInfo, 1, 4)
			.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.Add(fBrightnessSlider)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(workspacesLabel)
			.Add(fColumnsControl)
			.Add(fRowsControl)
			.AddGlue()
			.End()
		.AddGlue()
		.View();
}


/*!	Fills the resolution, colors and refresh rate menus from the mode list
	of the screen, the way it is done without a display layout.
*/
void
ScreenWindow::_BuildFallbackMenus()
{
	BScreen screen(this);

	std::vector<display_mode_entry> resolutions;
	int32 previousWidth = 0;
	int32 previousHeight = 0;
	for (int32 i = 0; i < fScreenMode.CountModes(); i++) {
		screen_mode mode = fScreenMode.ModeAt(i);
		if (mode.width == previousWidth && mode.height == previousHeight)
			continue;
		previousWidth = mode.width;
		previousHeight = mode.height;

		display_mode_entry entry;
		entry.width = mode.width;
		entry.height = mode.height;
		entry.refresh = mode.refresh;
		resolutions.push_back(entry);
	}
	_BuildResolutionMenu(resolutions);

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		BMessage* message = new BMessage(POP_COLORS_MSG);
		message->AddInt32("space", kColorSpaces[i].space);

		BMenuItem* item = new BMenuItem(kColorSpaces[i].label, message);
		if (kColorSpaces[i].space == screen.ColorSpace())
			fUserSelectedColorSpace = item;

		fColorsMenu->AddItem(item);
	}

	float min, max;
	if (fScreenMode.GetRefreshLimits(fActive, min, max) != B_OK) {
		// if we couldn't obtain the refresh limits, reset to the default
		// range. Constraints from detected monitors will fine-tune this
		// later.
		min = kRefreshRates[0];
		max = kRefreshRates[kRefreshRateCount - 1];
	}

	if (min == max) {
		// This is a special case for drivers that only support a single
		// frequency, like the VESA driver
		BString name;
		refresh_rate_to_string(min, name);
		BMessage* message = new BMessage(POP_REFRESH_MSG);
		message->AddFloat("refresh", min);
		BMenuItem* item = new BMenuItem(name.String(), message);
		fRefreshMenu->AddItem(item);
		item->SetEnabled(false);
	} else {
		monitor_info info;
		if (fScreenMode.GetMonitorInfo(info) == B_OK) {
			min = max_c(info.min_vertical_frequency, min);
			max = min_c(info.max_vertical_frequency, max);
		}

		for (int32 i = 0; i < kRefreshRateCount; ++i) {
			if (kRefreshRates[i] < min || kRefreshRates[i] > max)
				continue;

			BString name;
			name << kRefreshRates[i] << " " << B_TRANSLATE("Hz");

			BMessage* message = new BMessage(POP_REFRESH_MSG);
			message->AddFloat("refresh", kRefreshRates[i]);

			fRefreshMenu->AddItem(new BMenuItem(name.String(), message));
		}

		fOtherRefresh = new BMenuItem(B_TRANSLATE("Other" B_UTF8_ELLIPSIS),
			new BMessage(POP_OTHER_REFRESH_MSG));
		fRefreshMenu->AddItem(fOtherRefresh);
	}
}


/*!	Replaces the items of the resolution menu. With many resolutions, the
	menu is laid out as a matrix of three columns.
*/
void
ScreenWindow::_BuildResolutionMenu(
	const std::vector<display_mode_entry>& resolutions)
{
	fResolutionMenu->RemoveItems(0, fResolutionMenu->CountItems(), true);

	BRect itemRect;
	int32 rows = 1;
	if (fResolutionMatrix) {
		BFont menuFont;
		font_height fontHeight;

		fResolutionMenu->GetFont(&menuFont);
		menuFont.GetHeight(&fontHeight);
		itemRect.left = itemRect.top = 0;
		itemRect.bottom = fontHeight.ascent + fontHeight.descent + 4;
		itemRect.right = menuFont.StringWidth("99999x99999") + 16;
		rows = resolutions.size() / 3 + 1;
	}

	for (size_t i = 0; i < resolutions.size(); i++) {
		BMessage* message = new BMessage(POP_RESOLUTION_MSG);
		message->AddInt32("width", resolutions[i].width);
		message->AddInt32("height", resolutions[i].height);

		BString name;
		resolution_to_string(resolutions[i].width, resolutions[i].height,
			name);

		if (!fResolutionMatrix)
			fResolutionMenu->AddItem(new BMenuItem(name.String(), message));
		else {
			int32 y = i % rows;
			int32 x = i / rows;
			itemRect.OffsetTo(x * itemRect.Width(), y * itemRect.Height());
			fResolutionMenu->AddItem(new BMenuItem(name.String(), message),
				itemRect);
		}
	}
}


void
ScreenWindow::_BuildSupportedColorSpaces()
{
	fSupportedColorSpaces = 0;

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		for (int32 j = 0; j < fScreenMode.CountModes(); j++) {
			if (fScreenMode.ModeAt(j).space == kColorSpaces[i].space) {
				fSupportedColorSpaces |= 1 << i;
				break;
			}
		}
	}
}


//	#pragma mark - display layout


/*!	Reads the layout from the app_server. When it does not know about
	display layouts at all, a single display is made up from the screen.
*/
void
ScreenWindow::_LoadLayout()
{
	DisplayLayoutState state;
	if (state.Load() != B_OK) {
		BScreen screen(this);
		state.SetToSingleDisplay(screen.Frame(), fOriginal.width,
			fOriginal.height, fOriginal.refresh);

		monitor_info info;
		display_state* display = state.DisplayAt(0);
		if (display != NULL && screen.GetMonitorInfo(&info) == B_OK) {
			display->vendor = info.vendor;
			display->monitor = info.name;
			display->serial = info.serial_number;
			display->productID = info.product_id;
			display->week = info.produced.week;
			display->year = info.produced.year;
			display->widthCM = info.width;
			display->heightCM = info.height;
			display->hasEDID = true;
		}
	}

	fCurrentLayout = state;
	fPendingLayout = state;
	fOriginalLayout = state;
	fUndoLayout = state;
	fHasLayout = state.HasLayout();

	fSelectedID = state.PrimaryID();
	if (fSelectedID < 0)
		fSelectedID = state.FirstEnabledID();
	if (fSelectedID < 0 && state.CountDisplays() > 0)
		fSelectedID = state.DisplayAt(0)->id;
}


/*!	Reads the layout again after the screen changed - because we applied a
	layout, or because a monitor was plugged in or removed.
	Pending changes of the user survive as long as the set of displays did
	not change, unless \a resetPending is set.
*/
void
ScreenWindow::_ReloadLayout(bool resetPending)
{
	DisplayLayoutState state;
	if (state.Load() != B_OK) {
		if (!fHasLayout) {
			_UpdateActiveMode();
			_CheckApplyEnabled();
		}
		return;
	}

	bool changed = !state.SameArrangement(fCurrentLayout)
		|| state.HasLayout() != fCurrentLayout.HasLayout();
	bool zoomChanged
		= state.ZoomToDisplay() != fCurrentLayout.ZoomToDisplay();

	bool keepPending = !resetPending
		&& !fPendingLayout.SameArrangement(fCurrentLayout)
		&& state.SameIDs(fPendingLayout);

	// Only touch the views when something is actually different - a refresh
	// of the arrangement view would cancel a drag in progress
	bool refresh = changed || resetPending
		|| (!keepPending && !fPendingLayout.SameArrangement(state));

	fCurrentLayout = state;
	if (!keepPending)
		fPendingLayout = state;
	if (!state.SameIDs(fOriginalLayout)) {
		// The monitors changed; there is nothing to revert to anymore
		fOriginalLayout = state;
		fUndoLayout = state;
	}

	if (zoomChanged) {
		fZoomBox->SetValue(state.ZoomToDisplay()
			? B_CONTROL_ON : B_CONTROL_OFF);
	}

	if (!refresh)
		return;

	if (fPendingLayout.DisplayByID(fSelectedID) == NULL
		|| !fPendingLayout.DisplayByID(fSelectedID)->connected) {
		fSelectedID = fPendingLayout.PrimaryID();
		if (fSelectedID < 0)
			fSelectedID = fPendingLayout.FirstEnabledID();
	}

	if (!fHasLayout) {
		// resolution and refresh come from the screen mode in this case
		_UpdateActiveMode();
	}

	_UpdateArrangementView();
	_UpdateDetails();
	_CheckApplyEnabled();
}


void
ScreenWindow::_SelectDisplay(int32 id)
{
	if (fPendingLayout.DisplayByID(id) == NULL)
		return;

	fSelectedID = id;
	fArrangementView->SetSelectedID(id);
	_UpdateDetails();
}


display_state*
ScreenWindow::_SelectedDisplay()
{
	display_state* display = fPendingLayout.DisplayByID(fSelectedID);
	if (display == NULL) {
		for (int32 i = 0; i < fPendingLayout.CountDisplays(); i++) {
			if (fPendingLayout.DisplayAt(i)->connected) {
				display = fPendingLayout.DisplayAt(i);
				fSelectedID = display->id;
				break;
			}
		}
	}
	return display;
}


void
ScreenWindow::_UpdateArrangementView()
{
	fArrangementView->SetDisplays(fPendingLayout);
	fArrangementView->SetSelectedID(fSelectedID);
}


/*!	Shows the selected display in the details panel. */
void
ScreenWindow::_UpdateDetails()
{
	display_state* display = _SelectedDisplay();
	if (display == NULL)
		return;

	_UpdateTitle(*display);
	_UpdateInfo(*display);

	if (fHasLayout)
		_UpdateLayoutMenus(*display);

	// scale

	BMenuItem* scaleItem = NULL;
	for (int32 i = 0; i < fScaleMenu->CountItems(); i++) {
		BMenuItem* item = fScaleMenu->ItemAt(i);
		int32 scale;
		if (item->Message() != NULL
			&& item->Message()->FindInt32("scale", &scale) == B_OK
			&& scale == display->scale)
			scaleItem = item;
	}
	if (scaleItem != NULL)
		scaleItem->SetMarked(true);
	else {
		BMenuItem* marked = fScaleMenu->FindMarked();
		if (marked != NULL)
			marked->SetMarked(false);
		BString label;
		label.SetToFormat("%" B_PRId32 "%%", display->scale);
		fScaleMenu->Superitem()->SetLabel(label.String());
	}

	// enabled / main display

	bool lastEnabled = display->enabled
		&& fPendingLayout.CountEnabled() <= 1;
	fEnabledBox->SetValue(display->enabled ? B_CONTROL_ON : B_CONTROL_OFF);
	fEnabledBox->SetEnabled(fHasLayout && !lastEnabled);
	fEnabledBox->SetToolTip(lastEnabled
		? B_TRANSLATE("The last enabled display cannot be turned off.")
		: NULL);

	fPrimaryBox->SetValue(display->primary ? B_CONTROL_ON : B_CONTROL_OFF);
	fPrimaryBox->SetEnabled(fHasLayout && display->enabled
		&& !display->primary);

	// controls that make no sense for a disabled display; a mirror is as
	// large as its source's part of the desktop allows
	fResolutionField->SetEnabled(display->enabled);
	fRefreshField->SetEnabled(display->enabled
		&& fRefreshMenu->CountItems() > 0);
	fScaleField->SetEnabled(fCurrentLayout.CanScale() && display->enabled
		&& !display->IsMirror());
	if (display->IsMirror()) {
		fScaleField->SetToolTip(B_TRANSLATE("A mirror shows the main "
			"display's desktop as large as it fits."));
	} else if (fCurrentLayout.CanScale())
		fScaleField->SetToolTip((const char*)NULL);

	_UpdateMirrorBox();
}


void
ScreenWindow::_UpdateMirrorBox()
{
	bool canMirror = fPendingLayout.CanMirror();
	if (canMirror && fMirrorBox->IsHidden(fMirrorBox))
		fMirrorBox->Show();
	else if (!canMirror && !fMirrorBox->IsHidden(fMirrorBox))
		fMirrorBox->Hide();
	fMirrorBox->SetValue(fPendingLayout.MirrorState());
}


/*!	Fills the resolution and refresh rate menus with the modes the monitor
	accepts, and marks the pending mode.
*/
void
ScreenWindow::_UpdateLayoutMenus(const display_state& display)
{
	std::vector<display_mode_entry> resolutions;
	collect_resolutions(display.modes, resolutions);

	if (!display.HasMode(display.modeWidth, display.modeHeight)
		&& display.modeWidth > 0 && display.modeHeight > 0) {
		display_mode_entry current;
		current.width = display.modeWidth;
		current.height = display.modeHeight;
		current.refresh = display.modeRefresh;
		resolutions.push_back(current);
		std::vector<display_mode_entry> sorted;
		collect_resolutions(resolutions, sorted);
		resolutions = sorted;
	}

	_BuildResolutionMenu(resolutions);

	for (int32 i = 0; i < fResolutionMenu->CountItems(); i++) {
		BMenuItem* item = fResolutionMenu->ItemAt(i);
		int32 width, height;
		if (item->Message()->FindInt32("width", &width) == B_OK
			&& item->Message()->FindInt32("height", &height) == B_OK
			&& width == display.modeWidth && height == display.modeHeight) {
			item->SetMarked(true);
			break;
		}
	}

	// refresh rates at that resolution

	fRefreshMenu->RemoveItems(0, fRefreshMenu->CountItems(), true);
	fOtherRefresh = NULL;

	std::vector<float> rates;
	collect_refresh_rates(display.modes, display.modeWidth,
		display.modeHeight, rates);

	bool found = false;
	for (size_t i = 0; i < rates.size(); i++) {
		if (refresh_rates_equal(rates[i], display.modeRefresh))
			found = true;
	}
	if (!found && display.modeRefresh > 0) {
		rates.push_back(display.modeRefresh);
		std::sort(rates.begin(), rates.end(), std::greater<float>());
	}

	for (size_t i = 0; i < rates.size(); i++) {
		BString name;
		refresh_rate_to_string(rates[i], name);

		BMessage* message = new BMessage(POP_REFRESH_MSG);
		message->AddFloat("refresh", rates[i]);

		BMenuItem* item = new BMenuItem(name.String(), message);
		fRefreshMenu->AddItem(item);
		if (refresh_rates_equal(rates[i], display.modeRefresh))
			item->SetMarked(true);
	}
}


void
ScreenWindow::_UpdateTitle(const display_state& display)
{
	BString vendor;
	if (display.vendor.Length() > 0) {
		const char* name
			= fScreenMode.GetManufacturerFromID(display.vendor.String());
		vendor = name != NULL ? name : display.vendor.String();
	}

	// Remove extraneous vendor strings and whitespace from the model: the
	// EDID name is "DELL P2415Q" while the vendor list says "Dell Inc.", so
	// the vendor's first word goes too.
	BString model = display.monitor;
	if (vendor.Length() > 0) {
		model.IReplaceAll(vendor.String(), "");
		BString firstWord = vendor;
		int32 space = firstWord.FindFirst(' ');
		if (space > 0)
			firstWord.Truncate(space);
		if (firstWord.Length() >= 3 && model.IFindFirst(firstWord) == 0)
			model.Remove(0, firstWord.Length());
	}
	if (display.vendor.Length() > 0 && model.IFindFirst(display.vendor) == 0)
		model.Remove(0, display.vendor.Length());
	model.Trim();

	BString title;
	if (vendor.Length() > 0 && model.Length() > 0)
		title << vendor << " " << model;
	else if (model.Length() > 0)
		title = model;
	else if (vendor.Length() > 0)
		title = vendor;
	else if (display.name.Length() > 0)
		title = display.name;
	else
		title = B_TRANSLATE("Display");

	fTitleView->SetText(title.String());

	BString subtitle;
	BString part;

	float diagonal = display.DiagonalInches();
	if (diagonal > 0) {
		part.SetToFormat("%g\"", diagonal);
		add_part(subtitle, part);
	}

	if (display.name.Length() > 0 && display.name != title)
		add_part(subtitle, display.name);

	if (display.nativeWidth > 0 && display.nativeHeight > 0) {
		part.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32 " × %" B_PRId32
			" native", "The '×' is the Unicode multiplication sign U+00D7"),
			display.nativeWidth, display.nativeHeight);
		add_part(subtitle, part);
	}

	int32 dpi = display.DPI();
	if (dpi > 0) {
		part.SetToFormat(B_TRANSLATE("%" B_PRId32 " dpi"), dpi);
		add_part(subtitle, part);
	}

	if (display.IsMirror()) {
		part.SetToFormat(B_TRANSLATE("mirrors display %" B_PRId32),
			fPendingLayout.NumberOf(display.mirrorOf));
		add_part(subtitle, part);
	}

	fSubtitleView->SetText(subtitle.String());
	if (subtitle.Length() == 0) {
		if (!fSubtitleView->IsHidden(fSubtitleView))
			fSubtitleView->Hide();
	} else if (fSubtitleView->IsHidden(fSubtitleView))
		fSubtitleView->Show();
}


/*!	The read-only block: connector, serial number, manufacturing date and
	physical size - each only when known.
*/
void
ScreenWindow::_UpdateInfo(const display_state& display)
{
	struct {
		BStringView*	label;
		BStringView*	value;
		BString			text;
	} rows[4];

	rows[0].label = fConnectorLabel;
	rows[0].value = fConnectorView;
	rows[0].text = display.name;

	rows[1].label = fSerialLabel;
	rows[1].value = fSerialView;
	rows[1].text = display.serial;

	rows[2].label = fManufacturedLabel;
	rows[2].value = fManufacturedView;
	if (display.week > 0 && display.year > 0) {
		rows[2].text = B_TRANSLATE("week %week of %year");
		BString number;
		number << display.week;
		rows[2].text.ReplaceFirst("%week", number);
		number.SetTo("");
		number << display.year;
		rows[2].text.ReplaceFirst("%year", number);
	} else if (display.year > 0)
		rows[2].text << display.year;

	rows[3].label = fSizeLabel;
	rows[3].value = fSizeView;
	if (display.widthCM > 0 && display.heightCM > 0) {
		rows[3].text.SetToFormat(B_TRANSLATE_COMMENT("%.1f × %.1f cm",
			"The '×' is the Unicode multiplication sign U+00D7"),
			display.widthCM, display.heightCM);
	}

	for (size_t i = 0; i < B_COUNT_OF(rows); i++) {
		rows[i].value->SetText(rows[i].text.String());
		bool hide = rows[i].text.Length() == 0;
		BView* views[] = { rows[i].label, rows[i].value };
		for (size_t j = 0; j < 2; j++) {
			if (hide && !views[j]->IsHidden(views[j]))
				views[j]->Hide();
			else if (!hide && views[j]->IsHidden(views[j]))
				views[j]->Show();
		}
	}
}


void
ScreenWindow::_ApplyLayout()
{
	if (fPendingLayout.SameArrangement(fCurrentLayout))
		return;

	int32 mirrorID, sourceID;
	if (fPendingLayout.FindTooSmallMirror(mirrorID, sourceID)) {
		BString text = B_TRANSLATE("Display %mirror% has fewer pixels than "
			"what display %source% shows, and cannot mirror it.\n\n"
			"Choose a larger scale for display %source%, or a higher "
			"resolution for display %mirror%.");
		BString number;
		number << fPendingLayout.NumberOf(mirrorID);
		text.ReplaceAll("%mirror%", number);
		number.SetTo("");
		number << fPendingLayout.NumberOf(sourceID);
		text.ReplaceAll("%source%", number);
		BAlert* alert = new BAlert(B_TRANSLATE("Mirror displays"),
			text.String(), B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
		return;
	}

	// make checkpoint, so we can undo these changes
	fUndoLayout = fCurrentLayout;
	_ApplyLayoutState(fPendingLayout, true);
}


/*!	Sends \a state to the app_server. With \a showAlert, the user gets the
	usual countdown to confirm the new layout.
*/
void
ScreenWindow::_ApplyLayoutState(const DisplayLayoutState& state,
	bool showAlert)
{
	BMessage request;
	state.BuildRequest(request);

	status_t status = BPrivate::set_display_layout(request);
	if (status != B_OK) {
		_ShowError(B_TRANSLATE("The display layout could not be set:\n\t%s\n"),
			status);
		_UpdateArrangementView();
		_UpdateDetails();
		_CheckApplyEnabled();
		return;
	}

	_ReloadLayout(true);

	if (showAlert) {
		fModified = true;
		BAlert* window = new AlertWindow(this);
		window->Go(NULL);
	}
}


/*!	Opens a badge with the display's number on every enabled display. */
void
ScreenWindow::_IdentifyDisplays()
{
	int32 number = 1;
	for (int32 i = 0; i < fCurrentLayout.CountDisplays(); i++) {
		const display_state* display = fCurrentLayout.DisplayAt(i);
		if (!display->connected)
			continue;

		int32 thisNumber = number++;
		if (!display->enabled || !display->frame.IsValid()
			|| display->IsMirror())
			continue;

		// Mirrors show the badge of their source, and their numbers on it.
		BString numbers;
		numbers << thisNumber;
		BString label = display->monitor.Length() > 0
			? display->monitor : display->name;
		for (int32 j = 0; j < fCurrentLayout.CountDisplays(); j++) {
			const display_state* mirror = fCurrentLayout.DisplayAt(j);
			if (!mirror->connected || !mirror->enabled
				|| mirror->mirrorOf != display->id)
				continue;
			numbers << " | " << fCurrentLayout.NumberOf(mirror->id);
			label = B_TRANSLATE("Mirrored");
		}

		IdentifyWindow* window = new IdentifyWindow(numbers.String(),
			label.String(), display->frame);
		window->Show();
	}
}


void
ScreenWindow::_LaunchBackgrounds()
{
	if (be_roster->Launch(kBackgroundsSignature) == B_ALREADY_RUNNING) {
		app_info info;
		be_roster->GetAppInfo(kBackgroundsSignature, &info);
		be_roster->ActivateApp(info.team);
	}
}


//	#pragma mark - classic single screen path


/*!	Update resolution list according to combine mode
	(some resolutions may not be combinable due to memory restrictions).
*/
void
ScreenWindow::_CheckResolutionMenu()
{
	for (int32 i = 0; i < fResolutionMenu->CountItems(); i++)
		fResolutionMenu->ItemAt(i)->SetEnabled(false);

	for (int32 i = 0; i < fScreenMode.CountModes(); i++) {
		screen_mode mode = fScreenMode.ModeAt(i);
		if (mode.combine != fSelected.combine)
			continue;

		BString name;
		resolution_to_string(mode.width, mode.height, name);

		BMenuItem *item = fResolutionMenu->FindItem(name.String());
		if (item != NULL)
			item->SetEnabled(true);
	}
}


/*!	Update color and refresh options according to current mode
	(a color space is made active if there is any mode with
	given resolution and this colour space; same applies for
	refresh rate, though "Other…" is always possible)
*/
void
ScreenWindow::_CheckColorMenu()
{
	int32 supportsAnything = false;
	int32 index = 0;

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		bool supported = false;

		for (int32 j = 0; j < fScreenMode.CountModes(); j++) {
			screen_mode mode = fScreenMode.ModeAt(j);

			if (fSelected.width == mode.width
				&& fSelected.height == mode.height
				&& kColorSpaces[i].space == mode.space
				&& fSelected.combine == mode.combine) {
				supportsAnything = true;
				supported = true;
				break;
			}
		}

		BMenuItem* item = fColorsMenu->ItemAt(index++);
		if (item)
			item->SetEnabled(supported);
	}

	fColorsField->SetEnabled(supportsAnything);

	if (!supportsAnything)
		return;

	// Make sure a valid item is selected

	BMenuItem* item = fColorsMenu->FindMarked();
	bool changed = false;

	if (item != fUserSelectedColorSpace) {
		if (fUserSelectedColorSpace != NULL
			&& fUserSelectedColorSpace->IsEnabled()) {
			fUserSelectedColorSpace->SetMarked(true);
			item = fUserSelectedColorSpace;
			changed = true;
		}
	}
	if (item != NULL && !item->IsEnabled()) {
		// find the next best item
		int32 index = fColorsMenu->IndexOf(item);
		bool found = false;

		for (int32 i = index + 1; i < fColorsMenu->CountItems(); i++) {
			item = fColorsMenu->ItemAt(i);
			if (item->IsEnabled()) {
				found = true;
				break;
			}
		}
		if (!found) {
			// search backwards as well
			for (int32 i = index - 1; i >= 0; i--) {
				item = fColorsMenu->ItemAt(i);
				if (item->IsEnabled())
					break;
			}
		}

		item->SetMarked(true);
		changed = true;
	}

	if (changed) {
		// Update selected space

		BMessage* message = item->Message();
		int32 space;
		if (message->FindInt32("space", &space) == B_OK) {
			fSelected.space = (color_space)space;
			_UpdateColorLabel();
		}
	}
}


/*!	Enable/disable refresh options according to current mode. */
void
ScreenWindow::_CheckRefreshMenu()
{
	float min, max;
	if (fScreenMode.GetRefreshLimits(fSelected, min, max) != B_OK
		|| min == max)
		return;

	for (int32 i = fRefreshMenu->CountItems(); i-- > 0;) {
		BMenuItem* item = fRefreshMenu->ItemAt(i);
		BMessage* message = item->Message();
		float refresh;
		if (message != NULL && message->FindFloat("refresh", &refresh) == B_OK)
			item->SetEnabled(refresh >= min && refresh <= max);
	}
}


/*!	Activate appropriate menu item according to selected refresh rate */
void
ScreenWindow::_UpdateRefreshControl()
{
	if (isnan(fSelected.refresh)) {
		fRefreshMenu->SetEnabled(false);
		if (fOtherRefresh != NULL) {
			fOtherRefresh->SetLabel(B_TRANSLATE("Unknown"));
			fOtherRefresh->SetMarked(true);
		}
		return;
	} else {
		fRefreshMenu->SetEnabled(true);
	}

	for (int32 i = 0; i < fRefreshMenu->CountItems(); i++) {
		BMenuItem* item = fRefreshMenu->ItemAt(i);
		if (item->Message()->FindFloat("refresh") == fSelected.refresh) {
			item->SetMarked(true);
			// "Other" items only contains a refresh rate when active
			if (fOtherRefresh != NULL)
				fOtherRefresh->SetLabel(B_TRANSLATE("Other" B_UTF8_ELLIPSIS));
			return;
		}
	}

	// this is a non-standard refresh rate
	if (fOtherRefresh != NULL) {
		fOtherRefresh->Message()->ReplaceFloat("refresh", fSelected.refresh);
		fOtherRefresh->SetMarked(true);

		BString string;
		refresh_rate_to_string(fSelected.refresh, string);
		fRefreshMenu->Superitem()->SetLabel(string.String());

		string.Append(B_TRANSLATE("/other" B_UTF8_ELLIPSIS));
		fOtherRefresh->SetLabel(string.String());
	}
}


/*!	Reflects fSelected in the menus, and in the arrangement view. */
void
ScreenWindow::_UpdateFallbackControls()
{
	_UpdateWorkspaceButtons();

	BMenuItem* item = fSwapDisplaysMenu->ItemAt((int32)fSelected.swap_displays);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	item = fUseLaptopPanelMenu->ItemAt((int32)fSelected.use_laptop_panel);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	for (int32 i = 0; i < fTVStandardMenu->CountItems(); i++) {
		item = fTVStandardMenu->ItemAt(i);

		uint32 tvStandard;
		item->Message()->FindInt32("tv_standard", (int32 *)&tvStandard);
		if (tvStandard == fSelected.tv_standard) {
			if (!item->IsMarked())
				item->SetMarked(true);
			break;
		}
	}

	_CheckResolutionMenu();
	_CheckColorMenu();
	_CheckRefreshMenu();

	BString string;
	resolution_to_string(fSelected.width, fSelected.height, string);
	item = fResolutionMenu->FindItem(string.String());

	if (item != NULL) {
		if (!item->IsMarked())
			item->SetMarked(true);
	} else {
		// this is bad luck - if mode has been set via screen references,
		// this case cannot occur; there are three possible solutions:
		// 1. add a new resolution to list
		//    - we had to remove it as soon as a "valid" one is selected
		//    - we don't know which frequencies/bit depths are supported
		//    - as long as we haven't the GMT formula to create
		//      parameters for any resolution given, we cannot
		//      really set current mode - it's just not in the list
		// 2. choose nearest resolution
		//    - probably a good idea, but implies coding and testing
		// 3. choose lowest resolution
		//    - do you really think we are so lazy? yes, we are
		item = fResolutionMenu->ItemAt(0);
		if (item)
			item->SetMarked(true);

		// okay - at least we set menu label to active resolution
		fResolutionMenu->Superitem()->SetLabel(string.String());
	}

	// mark active combine mode
	for (int32 i = 0; i < kCombineModeCount; i++) {
		if (kCombineModes[i].mode == fSelected.combine) {
			item = fCombineMenu->ItemAt(i);
			if (item != NULL && !item->IsMarked())
				item->SetMarked(true);
			break;
		}
	}

	item = fColorsMenu->ItemAt(0);

	for (int32 i = 0, index = 0; i <  kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		if (kColorSpaces[i].space == fSelected.space) {
			item = fColorsMenu->ItemAt(index);
			break;
		}

		index++;
	}

	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	_UpdateColorLabel();
	_UpdateRefreshControl();

	// show the selected resolution in the arrangement view
	display_state* display = _SelectedDisplay();
	if (display != NULL && (display->modeWidth != fSelected.width
			|| display->modeHeight != fSelected.height)) {
		display->modeWidth = fSelected.width;
		display->modeHeight = fSelected.height;
		display->modeRefresh = fSelected.refresh;
		display->UpdateFrameSize();
		_UpdateArrangementView();
	}

	_CheckApplyEnabled();
}


/*! Reflect active mode in chosen settings */
void
ScreenWindow::_UpdateActiveMode()
{
	_UpdateActiveMode(current_workspace());
}


void
ScreenWindow::_UpdateActiveMode(int32 workspace)
{
	// Usually, this function gets called after a mode
	// has been set manually; still, as the graphics driver
	// is free to fiddle with mode passed, we better ask
	// what kind of mode we actually got
	if (fScreenMode.Get(fActive, workspace) == B_OK) {
		fSelected = fActive;

		_BuildSupportedColorSpaces();
		_UpdateFallbackControls();
	}
}


void
ScreenWindow::_UpdateOriginal()
{
	BPrivate::get_workspaces_layout(&fOriginalWorkspacesColumns,
		&fOriginalWorkspacesRows);

	fScreenMode.Get(fOriginal);
	fScreenMode.UpdateOriginalModes();
}


void
ScreenWindow::_UpdateColorLabel()
{
	if (fColorsMenu->Superitem() == NULL)
		return;

	BString string;
	string << fSelected.BitsPerPixel() << " " << B_TRANSLATE("bits/pixel");
	fColorsMenu->Superitem()->SetLabel(string.String());
}


void
ScreenWindow::_ApplyMode()
{
	// make checkpoint, so we can undo these changes
	fUndoScreenMode.UpdateOriginalModes();

	status_t status = fScreenMode.Set(fSelected);
	if (status == B_OK) {
		// use the mode that has eventually been set and
		// thus we know to be working; it can differ from
		// the mode selected by user due to hardware limitation
		display_mode newMode;
		BScreen screen(this);
		screen.GetMode(&newMode);

		if (fAllWorkspacesItem->IsMarked()) {
			int32 originatingWorkspace = current_workspace();
			const int32 workspaceCount = count_workspaces();
			for (int32 i = 0; i < workspaceCount; i++) {
				if (i != originatingWorkspace)
					screen.SetMode(i, &newMode, true);
			}
			fBootWorkspaceApplied = true;
		} else {
			if (current_workspace() == 0)
				fBootWorkspaceApplied = true;
		}

		fActive = fSelected;

		// TODO: only show alert when this is an unknown mode
		BAlert* window = new AlertWindow(this);
		window->Go(NULL);
	} else {
		_ShowError(B_TRANSLATE("The screen mode could not be set:\n\t%s\n"),
			status);
	}
}


status_t
ScreenWindow::_WriteVesaModeFile(const screen_mode& mode) const
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status < B_OK)
		return status;

	path.Append("kernel/drivers");
	status = create_directory(path.Path(), 0755);
	if (status < B_OK)
		return status;

	path.Append("vesa");
	BFile file;
	status = file.SetTo(path.Path(), B_CREATE_FILE | B_WRITE_ONLY | B_ERASE_FILE);
	if (status < B_OK)
		return status;

	char buffer[256];
	snprintf(buffer, sizeof(buffer), "mode %" B_PRId32 " %" B_PRId32 " %"
		B_PRId32 "\n", mode.width, mode.height, mode.BitsPerPixel());

	ssize_t bytesWritten = file.Write(buffer, strlen(buffer));
	if (bytesWritten < B_OK)
		return bytesWritten;

	return B_OK;
}


//	#pragma mark - both paths


void
ScreenWindow::_UpdateWorkspaceButtons()
{
	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);

	// Set the max values enabling/disabling the up/down arrows

	if (rows == 1)
		fColumnsControl->SetMaxValue(32);
	else if (rows == 2)
		fColumnsControl->SetMaxValue(16);
	else if (rows <= 4)
		fColumnsControl->SetMaxValue(8);
	else if (rows <= 8)
		fColumnsControl->SetMaxValue(4);
	else if (rows <= 16)
		fColumnsControl->SetMaxValue(2);
	else if (rows <= 32)
		fColumnsControl->SetMaxValue(1);

	if (columns == 1)
		fRowsControl->SetMaxValue(32);
	else if (columns == 2)
		fRowsControl->SetMaxValue(16);
	else if (columns <= 4)
		fRowsControl->SetMaxValue(8);
	else if (columns <= 8)
		fRowsControl->SetMaxValue(4);
	else if (columns <= 16)
		fRowsControl->SetMaxValue(2);
	else if (columns <= 32)
		fRowsControl->SetMaxValue(1);
}


void
ScreenWindow::_CheckApplyEnabled()
{
	bool applyEnabled = true;
	bool revertEnabled = false;

	if (fHasLayout) {
		applyEnabled = !fPendingLayout.SameArrangement(fCurrentLayout);
		revertEnabled = !fPendingLayout.SameArrangement(fOriginalLayout)
			|| !fCurrentLayout.SameArrangement(fOriginalLayout);
	} else {
		if (fSelected == fActive) {
			applyEnabled = false;
			if (fAllWorkspacesItem->IsMarked()) {
				screen_mode screenMode;
				const int32 workspaceCount = count_workspaces();
				for (int32 i = 0; i < workspaceCount; i++) {
					fScreenMode.Get(screenMode, i);
					if (screenMode != fSelected) {
						applyEnabled = true;
						break;
					}
				}
			}
		}
		revertEnabled = fSelected != fOriginal;

		// The one display can still be scaled
		if (_ScaleChanged())
			applyEnabled = true;
		if (!fPendingLayout.SameArrangement(fOriginalLayout)
			|| !fCurrentLayout.SameArrangement(fOriginalLayout))
			revertEnabled = true;
	}

	fApplyButton->SetEnabled(applyEnabled);

	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);

	BScreen screen(this);
	float brightness = -1;
	screen.GetBrightness(&brightness);

	fRevertButton->SetEnabled(revertEnabled
		|| columns != fOriginalWorkspacesColumns
		|| rows != fOriginalWorkspacesRows
		|| brightness != fOriginalBrightness
		|| (fDarkModeBox->Value() == B_CONTROL_ON) != fOriginalDarkMode);
}


/*!	Whether the user picked another scale for the single display of a
	driver without layouts.
*/
bool
ScreenWindow::_ScaleChanged() const
{
	if (fHasLayout || !fCurrentLayout.CanScale())
		return false;

	// Only the scale counts: the pending layout also carries the resolution
	// picked in the menu, which is set as a screen mode, not with the layout.
	const display_state* pending = fPendingLayout.DisplayByID(fSelectedID);
	const display_state* current = fCurrentLayout.DisplayByID(fSelectedID);
	return pending != NULL && current != NULL
		&& pending->scale != current->scale;
}


void
ScreenWindow::_ShowError(const char* format, status_t status)
{
	char message[256];
	snprintf(message, sizeof(message), format, screen_errors(status));
	BAlert* alert = new BAlert(B_TRANSLATE("Warning"), message,
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


//	#pragma mark - BWindow


void
ScreenWindow::ScreenChanged(BRect frame, color_space mode)
{
	// move window on screen, if necessary
	if (frame.right <= Frame().right
		&& frame.bottom <= Frame().bottom) {
		MoveTo((frame.Width() - Frame().Width()) / 2,
			(frame.Height() - Frame().Height()) / 2);
	}

	// The layout is read again a moment later - the frame changes when a
	// layout is applied, or when a monitor is plugged in or removed.
	delete fReloadRunner;
	fReloadRunner = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgReloadLayout), kReloadDelay, 1);
}


void
ScreenWindow::WorkspaceActivated(int32 workspace, bool state)
{
	if (fHasLayout || !state)
		return;

	if (fScreenMode.GetOriginalMode(fOriginal, workspace) == B_OK)
		_UpdateActiveMode(workspace);
}


void
ScreenWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);

	if (active && fHasLayout)
		_ReloadLayout(false);
}


void
ScreenWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case WORKSPACE_CHECK_MSG:
			_CheckApplyEnabled();
			break;

		case kMsgWorkspaceColumnsChanged:
		{
			uint32 newColumns = (uint32)fColumnsControl->Value();

			uint32 rows;
			BPrivate::get_workspaces_layout(NULL, &rows);
			BPrivate::set_workspaces_layout(newColumns, rows);

			_UpdateWorkspaceButtons();
			fRowsControl->SetValue(rows);
				// enables/disables up/down arrows
			_CheckApplyEnabled();
			break;
		}

		case kMsgWorkspaceRowsChanged:
		{
			uint32 newRows = (uint32)fRowsControl->Value();

			uint32 columns;
			BPrivate::get_workspaces_layout(&columns, NULL);
			BPrivate::set_workspaces_layout(columns, newRows);

			_UpdateWorkspaceButtons();
			fColumnsControl->SetValue(columns);
				// enables/disables up/down arrows
			_CheckApplyEnabled();
			break;
		}

		case kMsgDisplaySelected:
		{
			int32 id;
			if (message->FindInt32("id", &id) == B_OK)
				_SelectDisplay(id);
			break;
		}

		case kMsgDisplayMoved:
		{
			int32 id;
			BRect frame;
			if (message->FindInt32("id", &id) != B_OK
				|| message->FindRect("frame", &frame) != B_OK)
				break;

			display_state* display = fPendingLayout.DisplayByID(id);
			if (display == NULL)
				break;

			display->frame.OffsetTo(frame.LeftTop());
			fPendingLayout.Normalize();
			_UpdateArrangementView();
			_CheckApplyEnabled();
			break;
		}

		case kMsgDisplayEnabled:
		{
			display_state* display = _SelectedDisplay();
			if (display == NULL)
				break;

			fPendingLayout.SetEnabled(display->id,
				fEnabledBox->Value() == B_CONTROL_ON);
			fPendingLayout.Normalize();
			_UpdateArrangementView();
			_UpdateDetails();
			_CheckApplyEnabled();
			break;
		}

		case kMsgDisplayPrimary:
		{
			display_state* display = _SelectedDisplay();
			if (display == NULL)
				break;

			if (fPrimaryBox->Value() == B_CONTROL_ON) {
				fPendingLayout.SetPrimary(display->id);
				fPendingLayout.Normalize();
			}

			_UpdateArrangementView();
			_UpdateDetails();
			_CheckApplyEnabled();
			break;
		}

		case kMsgScaleChanged:
		{
			int32 scale;
			display_state* display = _SelectedDisplay();
			if (display == NULL
				|| message->FindInt32("scale", &scale) != B_OK)
				break;

			display->scale = scale;
			display->UpdateFrameSize();
			fPendingLayout.Normalize();
			_UpdateArrangementView();
			_CheckApplyEnabled();
			break;
		}

		case kMsgIdentifyDisplays:
			_IdentifyDisplays();
			break;

		case kMsgZoomToDisplay:
			BPrivate::set_zoom_to_display(fZoomBox->Value() == B_CONTROL_ON);
			break;

		case kMsgDarkMode:
			// takes effect at once, like the other appearance settings
			set_ui_dark_mode(fDarkModeBox->Value() == B_CONTROL_ON);
			_CheckApplyEnabled();
			break;

		case B_COLORS_UPDATED:
			// the appearance may have been switched elsewhere
			fDarkModeBox->SetValue(ui_dark_mode() ? B_CONTROL_ON : B_CONTROL_OFF);
			_CheckApplyEnabled();
			BWindow::MessageReceived(message);
			break;

		case kMsgMirrorDisplays:
			fPendingLayout.SetMirrored(fMirrorBox->Value() != B_CONTROL_OFF);
			_UpdateArrangementView();
			_UpdateDetails();
			_CheckApplyEnabled();
			break;

		case kMsgReloadLayout:
			delete fReloadRunner;
			fReloadRunner = NULL;
			_ReloadLayout(false);
			break;

		case POP_RESOLUTION_MSG:
		{
			int32 width, height;
			if (message->FindInt32("width", &width) != B_OK
				|| message->FindInt32("height", &height) != B_OK)
				break;

			if (fHasLayout) {
				display_state* display = _SelectedDisplay();
				if (display == NULL)
					break;

				display->modeWidth = width;
				display->modeHeight = height;

				// keep the refresh rate if the monitor offers it at this
				// resolution, else take the highest one
				std::vector<float> rates;
				collect_refresh_rates(display->modes, width, height, rates);
				bool found = false;
				for (size_t i = 0; i < rates.size(); i++) {
					if (refresh_rates_equal(rates[i], display->modeRefresh))
						found = true;
				}
				if (!found && !rates.empty())
					display->modeRefresh = rates[0];

				display->UpdateFrameSize();
				fPendingLayout.Normalize();
				_UpdateLayoutMenus(*display);
				_UpdateArrangementView();
				_CheckApplyEnabled();
				break;
			}

			fSelected.width = width;
			fSelected.height = height;

			_CheckColorMenu();
			_CheckRefreshMenu();
			_UpdateRefreshControl();

			display_state* display = _SelectedDisplay();
			if (display != NULL) {
				display->modeWidth = width;
				display->modeHeight = height;
				display->UpdateFrameSize();
				_UpdateArrangementView();
			}

			_CheckApplyEnabled();
			break;
		}

		case POP_COLORS_MSG:
		{
			int32 space;
			if (message->FindInt32("space", &space) != B_OK)
				break;

			int32 index;
			if (message->FindInt32("index", &index) == B_OK
				&& fColorsMenu->ItemAt(index) != NULL)
				fUserSelectedColorSpace = fColorsMenu->ItemAt(index);

			fSelected.space = (color_space)space;
			_UpdateColorLabel();

			_CheckApplyEnabled();
			break;
		}

		case POP_REFRESH_MSG:
		{
			float refresh;
			if (message->FindFloat("refresh", &refresh) != B_OK)
				break;

			if (fHasLayout) {
				display_state* display = _SelectedDisplay();
				if (display != NULL)
					display->modeRefresh = refresh;
				_CheckApplyEnabled();
				break;
			}

			fSelected.refresh = refresh;
			if (fOtherRefresh != NULL) {
				fOtherRefresh->SetLabel(B_TRANSLATE("Other" B_UTF8_ELLIPSIS));
					// revert "Other…" label - it might have a refresh rate
					// prefix
			}

			_CheckApplyEnabled();
			break;
		}

		case POP_OTHER_REFRESH_MSG:
		{
			// make sure menu shows something useful
			_UpdateRefreshControl();

			float min = 0, max = 999;
			fScreenMode.GetRefreshLimits(fSelected, min, max);
			if (min < gMinRefresh)
				min = gMinRefresh;
			if (max > gMaxRefresh)
				max = gMaxRefresh;

			monitor_info info;
			if (fScreenMode.GetMonitorInfo(info) == B_OK) {
				min = max_c(info.min_vertical_frequency, min);
				max = min_c(info.max_vertical_frequency, max);
			}

			RefreshWindow *fRefreshWindow = new RefreshWindow(
				fRefreshField->ConvertToScreen(B_ORIGIN), fSelected.refresh,
				min, max);
			fRefreshWindow->Show();
			break;
		}

		case SET_CUSTOM_REFRESH_MSG:
		{
			// user pressed "done" in "Other…" refresh dialog;
			// select the refresh rate chosen
			message->FindFloat("refresh", &fSelected.refresh);

			_UpdateRefreshControl();
			_CheckApplyEnabled();
			break;
		}

		case POP_COMBINE_DISPLAYS_MSG:
		{
			// new combine mode has bee chosen
			int32 mode;
			if (message->FindInt32("mode", &mode) == B_OK)
				fSelected.combine = (combine_mode)mode;

			_CheckResolutionMenu();
			_CheckApplyEnabled();
			break;
		}

		case POP_SWAP_DISPLAYS_MSG:
			message->FindBool("swap", &fSelected.swap_displays);
			_CheckApplyEnabled();
			break;

		case POP_USE_LAPTOP_PANEL_MSG:
			message->FindBool("use", &fSelected.use_laptop_panel);
			_CheckApplyEnabled();
			break;

		case POP_TV_STANDARD_MSG:
			message->FindInt32("tv_standard", (int32 *)&fSelected.tv_standard);
			_CheckApplyEnabled();
			break;

		case BUTTON_LAUNCH_BACKGROUNDS_MSG:
			_LaunchBackgrounds();
			break;

		case BUTTON_DEFAULTS_MSG:
		{
			if (fHasLayout) {
				fPendingLayout.SetDefaults();
				_UpdateArrangementView();
				_UpdateDetails();
				_CheckApplyEnabled();
				break;
			}

			// the preferred mode of the monitor, if we know it
			display_state* display = _SelectedDisplay();
			if (display != NULL && display->nativeWidth > 0
				&& display->nativeHeight > 0) {
				fSelected.width = display->nativeWidth;
				fSelected.height = display->nativeHeight;
				if (display->nativeRefresh > 0)
					fSelected.refresh = display->nativeRefresh;
			}
			fSelected.combine = kCombineDisable;
			fSelected.swap_displays = false;
			fSelected.use_laptop_panel = false;
			fSelected.tv_standard = 0;

			_UpdateFallbackControls();
			break;
		}

		case BUTTON_UNDO_MSG:
			if (fHasLayout || fUndoIsScale) {
				_ApplyLayoutState(fUndoLayout, false);
				break;
			}

			fUndoScreenMode.Revert();
			if (fUndoScaleAndMode) {
				// the scale changed with the mode, and goes back with it
				_ApplyLayoutState(fUndoLayout, false);
			}
			_UpdateActiveMode();
			break;

		case BUTTON_REVERT_MSG:
		{
			fModified = false;
			fBootWorkspaceApplied = false;

			// ScreenMode::Revert() assumes that we first set the correct
			// number of workspaces

			BPrivate::set_workspaces_layout(fOriginalWorkspacesColumns,
				fOriginalWorkspacesRows);
			_UpdateWorkspaceButtons();
			fColumnsControl->SetValue(fOriginalWorkspacesColumns);
			fRowsControl->SetValue(fOriginalWorkspacesRows);

			BScreen screen(this);
			if (fOriginalBrightness >= 0) {
				screen.SetBrightness(fOriginalBrightness);
				fBrightnessSlider->SetValue(fOriginalBrightness * 255);
			}

			if ((fDarkModeBox->Value() == B_CONTROL_ON) != fOriginalDarkMode) {
				set_ui_dark_mode(fOriginalDarkMode);
				fDarkModeBox->SetValue(fOriginalDarkMode
					? B_CONTROL_ON : B_CONTROL_OFF);
			}

			if (!fCurrentLayout.SameArrangement(fOriginalLayout))
				_ApplyLayoutState(fOriginalLayout, false);
			else {
				fPendingLayout = fCurrentLayout;
				_UpdateArrangementView();
				_UpdateDetails();
				_CheckApplyEnabled();
			}
			if (fHasLayout)
				break;

			fScreenMode.Revert();
			_UpdateActiveMode();
			break;
		}

		case BUTTON_APPLY_MSG:
		{
			if (fHasLayout) {
				_ApplyLayout();
				break;
			}

			// One display: its scale goes through the layout, its mode
			// through the classic path. Only one of them asks to keep the
			// change.
			bool scaleChanged = _ScaleChanged();
			bool modeChanged = fSelected != fActive;
			fUndoIsScale = scaleChanged && !modeChanged;
			fUndoScaleAndMode = scaleChanged && modeChanged;
			if (scaleChanged) {
				// The layout takes the new scale only. Applying it reads
				// the active mode back into the selection, so keep the
				// mode the user picked for _ApplyMode().
				screen_mode selected = fSelected;
				DisplayLayoutState scaled = fCurrentLayout;
				const display_state* pending
					= fPendingLayout.DisplayByID(fSelectedID);
				display_state* display = scaled.DisplayByID(fSelectedID);
				if (pending != NULL && display != NULL) {
					display->scale = pending->scale;
					display->UpdateFrameSize();
				}

				fUndoLayout = fCurrentLayout;
				_ApplyLayoutState(scaled, !modeChanged);
				fSelected = selected;
			}
			if (modeChanged || !scaleChanged)
				_ApplyMode();
			break;
		}

		case MAKE_INITIAL_MSG:
			// user pressed "keep" in confirmation dialog
			fModified = true;
			fOriginalLayout = fCurrentLayout;
			if (fHasLayout)
				_CheckApplyEnabled();
			else
				_UpdateActiveMode();
			break;

		case UPDATE_DESKTOP_COLOR_MSG:
			// the desktop color is no longer shown
			break;

		case SLIDER_BRIGHTNESS_MSG:
		{
			BScreen screen(this);
			screen.SetBrightness(message->FindInt32("be:value") / 255.f);
			_CheckApplyEnabled();
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}
