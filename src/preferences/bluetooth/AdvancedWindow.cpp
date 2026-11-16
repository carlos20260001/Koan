/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "AdvancedWindow.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <OptionPopUp.h>
#include <Roster.h>
#include <StringView.h>

#include <LELog.h>

#include <string.h>

#include "BluetoothSettingsView.h"
#include "defs.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Advanced window"


void
OpenLELog()
{
	const char* path = Bluetooth::LELogFilePath();
	BEntry entry(path);
	entry_ref ref;
	if (!entry.Exists() || entry.GetRef(&ref) != B_OK) {
		BString text(B_TRANSLATE("There is no Bluetooth log yet. It is "
			"created the first time a Low Energy device connects or "
			"pairs.\n\n%path%"));
		text.ReplaceFirst("%path%", path);
		BAlert* alert = new BAlert(B_TRANSLATE("Bluetooth log"), text,
			B_TRANSLATE("OK"));
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
		return;
	}

	// The log has no file type; open it in the text editor directly.
	BMessage refs(B_REFS_RECEIVED);
	refs.AddRef("refs", &ref);
	if (be_roster->Launch("application/x-vnd.vos-StyledEdit", &refs)
			!= B_OK)
		be_roster->Launch(&ref);
}


AdvancedWindow::AdvancedWindow(const BMessenger& mainWindow,
	LocalDevice* device)
	:
	BWindow(BRect(0, 0, 100, 100), B_TRANSLATE("Advanced Bluetooth settings"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS
			| B_CLOSE_ON_ESCAPE),
	fDevice(device)
{
	fSettingsView = new BluetoothSettingsView("settings", mainWindow);

	BBox* adapterBox = new BBox("adapter");
	adapterBox->SetLabel(B_TRANSLATE("This computer"));
	BLayoutBuilder::Group<>(adapterBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(fSettingsView)
	.End();

	fLogLevel = new BOptionPopUp("log level",
		B_TRANSLATE("Pairing log level:"), new BMessage(kMsgSetLogLevel));
	fLogLevel->AddOption(B_TRANSLATE("Errors only"), Bluetooth::LE_LOG_ERROR);
	fLogLevel->AddOption(B_TRANSLATE("Steps"), Bluetooth::LE_LOG_INFO);
	fLogLevel->AddOption(B_TRANSLATE("Debug (includes kernel traces)"),
		Bluetooth::LE_LOG_DEBUG);
	fLogLevel->AddOption(B_TRANSLATE("Packet trace"), Bluetooth::LE_LOG_TRACE);
	fLogLevel->SetValue(Bluetooth::LELogLevel());

	BStringView* logPath = new BStringView("log path",
		Bluetooth::LELogFilePath());
	logPath->SetHighUIColor(B_PANEL_TEXT_COLOR, B_LIGHTEN_1_TINT);
	logPath->SetTruncation(B_TRUNCATE_MIDDLE);

	BBox* diagnosticsBox = new BBox("diagnostics");
	diagnosticsBox->SetLabel(B_TRANSLATE("Diagnostics"));
	BLayoutBuilder::Group<>(diagnosticsBox, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(fLogLevel)
		.AddGroup(B_HORIZONTAL)
			.Add(logPath, 1.0f)
			.Add(new BButton("open log", B_TRANSLATE("Open log"),
				new BMessage(kMsgShowLog)))
		.End()
	.End();

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(adapterBox)
		.Add(diagnosticsBox)
	.End();

	CenterOnScreen();

	// Reading the adapter's properties waits for the server; do it on this
	// window's thread once it runs.
	PostMessage(kMsgLoadLocalDevice);
}


void
AdvancedWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgLoadLocalDevice:
			fSettingsView->SetLocalDevice(fDevice);
			break;

		case kMsgSetLogLevel:
		{
			int32 level;
			if (message->FindInt32("be:value", &level) != B_OK)
				break;
			status_t status = Bluetooth::LESetLogLevel(level);
			if (status != B_OK) {
				BString text(B_TRANSLATE("Could not save the log level: "
					"%error%"));
				text.ReplaceFirst("%error%", strerror(status));
				BAlert* alert = new BAlert(B_TRANSLATE("Bluetooth"), text,
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->Go(NULL);
				fLogLevel->SetValue(Bluetooth::LELogLevel());
			}
			break;
		}

		case kMsgShowLog:
			OpenLELog();
			break;

		case kMsgShowAdvanced:
			Activate();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
