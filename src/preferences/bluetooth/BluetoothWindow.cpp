/*
 * Copyright 2008-10, Oliver Ruiz Dorantes, <oliver.ruiz.dorantes_at_gmail.com>
 * Copyright 2026, Haiku, Inc.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "BluetoothWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <FindDirectory.h>
#include <Invoker.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <TextView.h>

#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>

#include <A2dpSource.h>
#include <LaunchRoster.h>
#include <LEBondStore.h>
#include <LELog.h>
#include <LEPairingSession.h>
#include <bluetoothserver_p.h>

#include <algorithm>
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "AdvancedWindow.h"
#include "BluetoothSettings.h"
#include "BluetoothWorkers.h"
#include "DeviceIcons.h"
#include "DeviceListItem.h"
#include "defs.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Window"


#define TRACE(format...) \
	Bluetooth::LELog(Bluetooth::LE_LOG_INFO, "prefs", format)


static const uint32 kMsgRemoveConfirmed = 'rmCf';

// The launch_daemon job that runs bluetooth_server
static const char* kServerJob = "x-vnd.vos-bluetooth_server";

static const bigtime_t kTickInterval = 2000000;
static const bigtime_t kServerStartDelay = 1500000;
static const bigtime_t kProbeTimeout = 6000000;
static const bigtime_t kNameLookupTimeout = 20000000;
static const bigtime_t kClassicConnectTimeout = 60000000;
static const bigtime_t kExpireAfter = 90000000;
	// The server restarts its filtered scan about every 10 seconds, and a
	// Classic inquiry runs every 30 seconds; a device missing for 90 seconds
	// has gone.
static const bigtime_t kExpireGrace = 60000000;


LocalDevice* ActiveLocalDevice = NULL;


static const char*
StageText(int32 stage)
{
	switch (stage) {
		case Bluetooth::LE_PAIRING_CONNECT:
			return B_TRANSLATE("Connecting");
		case Bluetooth::LE_PAIRING_BOND_LOOKUP:
			return B_TRANSLATE("Looking up saved keys");
		case Bluetooth::LE_PAIRING_SMP:
			return B_TRANSLATE("Starting pairing");
		case Bluetooth::LE_PAIRING_SMP_SOCKET:
			return B_TRANSLATE("Opening the pairing channel");
		case Bluetooth::LE_PAIRING_SMP_REQUEST:
			return B_TRANSLATE("Sending the pairing request");
		case Bluetooth::LE_PAIRING_SMP_RESPONSE:
			return B_TRANSLATE("Waiting for the device to answer");
		case Bluetooth::LE_PAIRING_SMP_CONFIRM:
			return B_TRANSLATE("Confirming pairing");
		case Bluetooth::LE_PAIRING_SMP_RANDOM:
			return B_TRANSLATE("Exchanging pairing values");
		case Bluetooth::LE_PAIRING_ENCRYPT:
			return B_TRANSLATE("Encrypting the connection");
		case Bluetooth::LE_PAIRING_KEYS:
			return B_TRANSLATE("Receiving keys");
		case Bluetooth::LE_PAIRING_SAVE:
			return B_TRANSLATE("Saving the pairing");
		case Bluetooth::LE_PAIRING_HID:
			return B_TRANSLATE("Reading the mouse services");
		case Bluetooth::LE_PAIRING_CLEANUP:
			return B_TRANSLATE("Closing the pairing channel");
		default:
			return B_TRANSLATE("Finishing");
	}
}


static const char*
PairingModeHint()
{
	return B_TRANSLATE("Put the device in pairing mode: for Logitech mice, "
		"press the Easy-Switch button to pick a channel, then hold the "
		"Connect button until the light blinks quickly.");
}


struct ServiceHook {
	BPath		path;
	BMessenger	target;
};


static int32
RunServiceStartHook(void* data)
{
	ServiceHook* hook = (ServiceHook*)data;
	char* const arguments[] = { (char*)"sh", (char*)hook->path.Path(), NULL };
	pid_t process;
	int exitCode = -1;
	int result = posix_spawn(&process, "/bin/sh", NULL, NULL, arguments,
		environ);
	if (result == 0) {
		int status;
		while (waitpid(process, &status, 0) < 0 && errno == EINTR) {}
		exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	}

	BMessage done(kMsgServiceHookDone);
	done.AddInt32("error", result);
	done.AddInt32("exit", exitCode);
	done.AddString("path", hook->path.Path());
	hook->target.SendMessage(&done);
	delete hook;
	return 0;
}


//	#pragma mark - HeaderIconView


class HeaderIconView : public BView {
public:
	HeaderIconView()
		:
		BView("icon", B_WILL_DRAW),
		fEnabled(false)
	{
		fSize = ceilf(DeviceIconSize() * 1.5f);
		SetExplicitSize(BSize(fSize - 1, fSize - 1));
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	void SetEnabled(bool enabled)
	{
		if (enabled == fEnabled)
			return;
		fEnabled = enabled;
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		const BBitmap* icon = BluetoothIcon(fSize);
		if (icon == NULL)
			return;
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(icon, BPoint(0, 0));
		if (!fEnabled) {
			rgb_color veil = ViewColor();
			veil.alpha = 160;
			SetHighColor(veil);
			FillRect(Bounds());
		}
	}

private:
	float	fSize;
	bool	fEnabled;
};


//	#pragma mark - BluetoothWindow


BluetoothWindow::BluetoothWindow()
	:
	BWindow(BRect(100, 100, 620, 700), B_TRANSLATE_SYSTEM_NAME("Bluetooth"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS),
	fTickRunner(NULL),
	fServerRunning(false),
	fRestartPending(false),
	fProbing(false),
	fProbeStarted(0),
	fProbeGeneration(0),
	fAdapterIndex(-1),
	fPairedGeneration(0),
	fSearchingSince(0),
	fNameLookupActive(false),
	fNameLookupStarted(0),
	fOperation(OPERATION_NONE),
	fOperationState(OPERATION_IDLE),
	fOperationKey(0),
	fOperationKind(DEVICE_KIND_GENERIC),
	fOperationFromPaired(false),
	fOperationAddressType(0),
	fOperationClass(0),
	fOperationRepetitionMode(0),
	fOperationClockOffset(0),
	fOperationStage(-1),
	fOperationStarted(0),
	fMinimized(false)
{
	fNames.Load();

	// Menu

	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* menu = new BMenu(B_TRANSLATE("Bluetooth"));
	menu->AddItem(new BMenuItem(
		B_TRANSLATE("Advanced settings" B_UTF8_ELLIPSIS),
		new BMessage(kMsgShowAdvanced), ','));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Open log"),
		new BMessage(kMsgShowLog)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Start Bluetooth services"),
		new BMessage(kMsgStartServices)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Stop Bluetooth services"),
		new BMessage(kMsgStopServices)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Refresh adapters"),
		new BMessage(kMsgRefresh), 'R'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("About Bluetooth" B_UTF8_ELLIPSIS),
		new BMessage(B_ABOUT_REQUESTED)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(menu);

	// Header

	fIconView = new HeaderIconView();
	fTitleView = new BStringView("title", "");
	BFont titleFont(be_bold_font);
	titleFont.SetSize(ceilf(be_bold_font->Size() * 1.2f));
	fTitleView->SetFont(&titleFont);
	fSubtitleView = new BStringView("subtitle", "");
	fSubtitleView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_1_TINT);

	fAdapterMenu = new BPopUpMenu(B_TRANSLATE("Adapter"));
	fAdapterField = new BMenuField("adapter", B_TRANSLATE("Adapter:"),
		fAdapterMenu);
	fAdapterField->Hide();

	fServiceButton = new BButton("service", B_TRANSLATE("Start service"),
		new BMessage(kMsgStartServices));
	fServiceButton->Hide();

	// My devices

	BStringView* pairedLabel = new BStringView("paired label",
		B_TRANSLATE("My devices"));
	pairedLabel->SetFont(be_bold_font);

	fPairedList = new DeviceListView("paired");
	fPairedList->SetSelectionMessage(new BMessage(kMsgPairedSelected));
	fPairedList->SetInvocationMessage(new BMessage(kMsgConnectPaired));
	fPairedList->SetEmptyText(B_TRANSLATE("No paired devices"));
	BScrollView* pairedScroll = new BScrollView("paired scroll", fPairedList,
		0, false, true);

	fConnectButton = new BButton("connect", B_TRANSLATE("Connect"),
		new BMessage(kMsgConnectPaired));
	fDisconnectButton = new BButton("disconnect", B_TRANSLATE("Disconnect"),
		new BMessage(kMsgDisconnectPaired));
	fRemoveButton = new BButton("remove", B_TRANSLATE("Remove" B_UTF8_ELLIPSIS),
		new BMessage(kMsgRemovePaired));

	// Nearby devices

	BStringView* nearbyLabel = new BStringView("nearby label",
		B_TRANSLATE("Nearby devices"));
	nearbyLabel->SetFont(be_bold_font);
	fScanStatusView = new BStringView("scan status", "");
	fScanStatusView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_1_TINT);

	fShowUnnamed = new BCheckBox("show unnamed",
		B_TRANSLATE("Show unnamed devices"), new BMessage(kMsgShowUnnamed));

	fNearbyList = new DeviceListView("nearby");
	fNearbyList->SetSelectionMessage(new BMessage(kMsgNearbySelected));
	fNearbyList->SetInvocationMessage(new BMessage(kMsgNearbyInvoked));
	BScrollView* nearbyScroll = new BScrollView("nearby scroll", fNearbyList,
		0, false, true);

	fPairButton = new BButton("pair", B_TRANSLATE("Connect"),
		new BMessage(kMsgConnectNearby));

	// Status area

	fStatusTitle = new BStringView("status title", "");
	fStatusTitle->SetFont(be_bold_font);
	fStatusText = new BTextView("status text");
	fStatusText->MakeEditable(false);
	fStatusText->MakeSelectable(true);
	fStatusText->SetWordWrap(true);
	fStatusText->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fStatusText->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
	fStatusText->SetFontAndColor(be_plain_font, B_FONT_ALL, &textColor);
	font_height fontHeight;
	be_plain_font->GetHeight(&fontHeight);
	float lineHeight = ceilf(fontHeight.ascent + fontHeight.descent
		+ fontHeight.leading);
	fStatusText->SetExplicitMinSize(BSize(B_SIZE_UNSET, lineHeight * 4));
	fShowLogButton = new BButton("show log", B_TRANSLATE("Show log"),
		new BMessage(kMsgShowLog));
	fDismissButton = new BButton("dismiss", B_TRANSLATE("OK"),
		new BMessage(kMsgDismissStatus));

	fStatusView = new BView("status", 0);
	BLayoutBuilder::Group<>(fStatusView, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.Add(fStatusTitle)
		.Add(fStatusText)
		.AddGroup(B_HORIZONTAL)
			.Add(fShowLogButton)
			.AddGlue()
			.Add(fDismissButton)
		.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
	.End();
	fStatusView->Hide();

	BButton* advancedButton = new BButton("advanced",
		B_TRANSLATE("Advanced" B_UTF8_ELLIPSIS), new BMessage(kMsgShowAdvanced));

	float rowHeight = ceilf(DeviceIconSize()
		+ be_control_look->DefaultLabelSpacing() * 2);
	pairedScroll->SetExplicitMinSize(BSize(rowHeight * 9, rowHeight * 2.2f));
	nearbyScroll->SetExplicitMinSize(BSize(rowHeight * 9, rowHeight * 7.2f));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.AddGroup(B_HORIZONTAL)
				.Add(fIconView)
				.AddGroup(B_VERTICAL, 0)
					.Add(fTitleView)
					.Add(fSubtitleView)
				.End()
				.AddGlue()
				.Add(fAdapterField)
				.Add(fServiceButton)
			.End()
			.Add(new BSeparatorView(B_HORIZONTAL))
			.Add(pairedLabel)
			.Add(pairedScroll, 1.0f)
			.AddGroup(B_HORIZONTAL)
				.Add(fRemoveButton)
				.AddGlue()
				.Add(fDisconnectButton)
				.Add(fConnectButton)
			.End()
			.AddStrut(B_USE_HALF_ITEM_SPACING)
			.AddGroup(B_HORIZONTAL)
				.Add(nearbyLabel)
				.Add(fScanStatusView)
				.AddGlue()
				.Add(fShowUnnamed)
			.End()
			.Add(nearbyScroll, 2.0f)
			.Add(fStatusView)
			.AddGroup(B_HORIZONTAL)
				.Add(advancedButton)
				.AddGlue()
				.Add(fPairButton)
			.End()
		.End()
	.End();

	fScanner = new NearbyScanner(this);
	AddHandler(fScanner);

	fTickRunner = new BMessageRunner(BMessenger(this), BMessage(kMsgTick),
		kTickInterval);
	be_roster->StartWatching(BMessenger(this),
		B_REQUEST_LAUNCHED | B_REQUEST_QUIT);

	_ServerStateChanged(be_roster->IsRunning(BLUETOOTH_SIGNATURE));
	_UpdatePairedButtons();
	_UpdateNearbyButtons();
	_UpdateScanStatus();
}


BluetoothWindow::~BluetoothWindow()
{
	delete fTickRunner;
	RemoveHandler(fScanner);
	delete fScanner;

	_ClearNearby();
	_ClearPaired();
}


bool
BluetoothWindow::QuitRequested()
{
	fScanner->Stop();
	fScanner->SetAdapter(NULL);

	if (fServerRunning) {
		BMessage stopWatching(BT_STOP_WATCHING_CONNECTIONS);
		stopWatching.AddMessenger("messenger", BMessenger(this));
		BMessenger(BLUETOOTH_SIGNATURE).SendMessage(&stopWatching,
			(BHandler*)NULL, 1000000);
	}
	be_roster->StopWatching(BMessenger(this));

	fNames.Save();

	if (fAdvancedWindow.IsValid())
		fAdvancedWindow.SendMessage(B_QUIT_REQUESTED);

	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
BluetoothWindow::Minimize(bool minimize)
{
	BWindow::Minimize(minimize);
	fMinimized = minimize;
	_UpdateScanning();
}


void
BluetoothWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTick:
		{
			bigtime_t now = system_time();
			if (fProbing && now - fProbeStarted > kProbeTimeout)
				_UpdateHeader();
			if (fNameLookupActive
				&& now - fNameLookupStarted > kNameLookupTimeout)
				fNameLookupActive = false;
			if (fOperation == OPERATION_CLASSIC_CONNECT
				&& fOperationState == OPERATION_RUNNING
				&& now - fOperationStarted > kClassicConnectTimeout)
				_ClassicConnectFinished(false, 0);
			_ExpireNearby();
			break;
		}

		case B_SOME_APP_LAUNCHED:
		case B_SOME_APP_QUIT:
		{
			const char* signature;
			if (message->FindString("be:signature", &signature) != B_OK
				|| strcasecmp(signature, BLUETOOTH_SIGNATURE) != 0)
				break;
			if (message->what == B_SOME_APP_QUIT)
				_ServerStateChanged(false);
			else {
				// Give the server time to register its adapters.
				BMessageRunner::StartSending(BMessenger(this),
					new BMessage(kMsgServerLaunched), kServerStartDelay, 1);
			}
			break;
		}

		case kMsgServerLaunched:
			_ServerStateChanged(be_roster->IsRunning(BLUETOOTH_SIGNATURE));
			break;

		case kMsgStartServices:
			_StartServices();
			break;

		case kMsgStopServices:
			_StopServices();
			break;

		case kMsgServiceHookDone:
		{
			int32 error = message->GetInt32("error", B_ERROR);
			int32 exitCode = message->GetInt32("exit", -1);
			bool running = be_roster->IsRunning(BLUETOOTH_SIGNATURE);
			if (error == 0 && exitCode == 0 && running) {
				_HideStatus();
				break;
			}
			if (error == 0 && exitCode == 0 && !message->HasBool("recheck")) {
				// The script may start the server in the background; give it
				// a moment to register.
				BMessage recheck(*message);
				recheck.AddBool("recheck", true);
				BMessageRunner::StartSending(BMessenger(this), &recheck,
					3000000, 1);
				break;
			}
			BString text;
			if (error != 0) {
				text = B_TRANSLATE("The start script %path% could not be run: "
					"%error%");
				text.ReplaceFirst("%error%", strerror(error));
			} else if (exitCode != 0) {
				text = B_TRANSLATE("The start script %path% failed (exit "
					"status %code%). The Bluetooth controller may need a "
					"restart of the computer.");
				text.ReplaceFirst("%code%", BString() << exitCode);
			} else {
				text = B_TRANSLATE("The start script %path% finished, but the "
					"Bluetooth service is not running.");
			}
			text.ReplaceFirst("%path%", message->GetString("path", ""));
			_ShowStatus(B_TRANSLATE("Could not start the Bluetooth service"),
				text, false);
			break;
		}

		case kMsgRestartServices:
			fRestartPending = true;
			if (fServerRunning)
				_StopServices();
			else
				_StartServices();
			break;

		case kMsgRefresh:
			_ProbeAdapters();
			_LoadPaired();
			break;

		case kMsgAdaptersProbed:
			_AdaptersProbed(message);
			break;

		case kMsgAdapterSelected:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK
				&& index != fAdapterIndex)
				_SelectAdapter(index);
			break;
		}

		case kMsgLocalNameChanged:
		{
			int32 id = message->GetInt32("id", -1);
			for (size_t i = 0; i < fAdapters.size(); i++) {
				if (fAdapters[i].id == id)
					fAdapters[i].name = message->GetString("name", "");
			}
			_UpdateHeader();
			break;
		}

		// My devices

		case kMsgPairedLoaded:
			_PairedLoaded(message);
			break;

		case kMsgPairedSelected:
			_UpdatePairedButtons();
			break;

		case kMsgConnectPaired:
			_ConnectPaired();
			break;

		case kMsgDisconnectPaired:
			_DisconnectPaired();
			break;

		case kMsgRemovePaired:
			_RemovePaired();
			break;

		case kMsgRemoveConfirmed:
		{
			if (message->GetInt32("which", 0) != 1)
				break;
			device_key key = message->GetUInt64("key", 0);
			std::map<device_key, PairedDevice*>::iterator found
				= fPaired.find(key);
			if (found == fPaired.end())
				break;
			PairedDevice* device = found->second;

			status_t status = B_OK;
			if (device->isLE) {
				char directory[B_PATH_NAME_LENGTH];
				status = Bluetooth::DefaultLEBondDirectory(directory,
					sizeof(directory));
				if (status == B_OK) {
					// Also removes the HID mouse marker, so the input_server
					// add-on stops reconnecting it.
					status = Bluetooth::RemoveLEBond(directory,
						device->leLocalAddress, device->leLocalAddressType,
						device->address, device->leAddressType);
				}
				if (status == B_OK && _HCIID() >= 0) {
					// End a link the add-on may hold to the forgotten
					// device right away instead of within a few seconds.
					BMessage disconnect(BT_MSG_LE_DISCONNECT);
					disconnect.AddInt32("hci_id", _HCIID());
					disconnect.AddMessenger("listener", BMessenger(this));
					disconnect.AddBool("force", true);
					BMessenger(BLUETOOTH_SIGNATURE).SendMessage(&disconnect,
						(BHandler*)NULL, 1000000);
				}
			} else {
				_ReleaseAudioSink(device->address);
				status = SendClassicRequest(BT_REQ_REMOVE_DEVICE, _HCIID(),
					device->address);
			}

			if (status != B_OK && status != B_ENTRY_NOT_FOUND) {
				BString title(B_TRANSLATE("Could not remove %name%"));
				title.ReplaceFirst("%name%", device->Name());
				_ShowStatus(title, strerror(status), false);
			} else {
				fNames.Remove(key);
				fNames.Save();
				fPairedList->RemoveItem(device->item);
				delete device->item;
				delete device;
				fPaired.erase(found);
				_UpdatePairedButtons();
				// The device may be advertising; list it as nearby again.
				_UpdateAllNearby();
			}
			_LoadPaired();
			break;
		}

		case BT_MSG_CONN_COMPLETED:
		case BT_MSG_CONN_FAILED:
		case BT_MSG_DISCONN_COMPLETED:
		case BT_MSG_NEW_REMOTE_DEVICE:
			_ConnectionEvent(message);
			break;

		// Nearby devices

		case kMsgNearbySelected:
			_UpdateNearbyButtons();
			break;

		case kMsgNearbyInvoked:
		case kMsgConnectNearby:
			_ConnectNearby();
			break;

		case kMsgShowUnnamed:
			_UpdateAllNearby();
			break;

		case kMsgNameResult:
			_NameResult(message);
			break;

		// Operations

		case Bluetooth::LE_PAIRING_PROGRESS_MESSAGE:
		case kMsgLEPairBusy:
			_PairingProgress(message);
			break;

		case kMsgLEPairDone:
			_LEPairDone(message);
			break;

		case kMsgClassicConnectSent:
			_ClassicConnectSent(message);
			break;

		case kMsgDismissStatus:
			if (fOperation == OPERATION_NONE)
				_HideStatus();
			break;

		case kMsgShowLog:
			OpenLELog();
			break;

		case kMsgShowAdvanced:
			_OpenAdvanced();
			break;

		case B_ABOUT_REQUESTED:
			be_app->PostMessage(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


//	#pragma mark - ScannerListener


void
BluetoothWindow::LEAdvertisement(const uint8 address[6], uint8 addressType,
	uint8 eventType, const LEAdvertisingData& advertising, bool hasRSSI,
	int8 rssi)
{
	// Resolvable private addresses change and cannot be paired with yet.
	if (addressType > 1)
		return;

	NearbyDevice* device = _Nearby(address);
	device->MergeAdvertisement(addressType, eventType, advertising, hasRSSI,
		rssi);
	if (device->HasName())
		_LearnName(device->key, device->Name(), device->Kind());
	_LinkCompanions(device);
	_UpdateNearbyItem(device);

	std::map<device_key, NearbyDevice*>::iterator twin
		= fNearby.find(device->companionOf);
	if (device->companionOf != 0 && twin != fNearby.end() && device->hasRSSI) {
		twin->second->hasRSSI = true;
		twin->second->rssi = device->rssi;
		twin->second->lastSeen = device->lastSeen;
		_UpdateNearbyItem(twin->second);
	}
}


void
BluetoothWindow::ClassicDeviceFound(const uint8 address[6],
	uint32 deviceClass, const BString& name, uint8 pageRepetitionMode,
	uint16 clockOffset)
{
	NearbyDevice* device = _Nearby(address);
	device->MergeClassic(deviceClass, name, pageRepetitionMode, clockOffset);
	_LinkCompanions(device);
	if (device->HasName())
		_LearnName(device->key, device->Name(), device->Kind());
	_UpdateNearbyItem(device);
}


void
BluetoothWindow::ClassicInquiryFinished()
{
	_StartNameLookup();
}


void
BluetoothWindow::ScannerStatusChanged()
{
	_UpdateScanStatus();
}


void
BluetoothWindow::ScannerPaused()
{
	if (fOperation != OPERATION_NONE && fOperationState == OPERATION_WAITING)
		_RunOperation();
}


//	#pragma mark - Service and adapters


void
BluetoothWindow::_ServerStateChanged(bool running)
{
	if (running && !fServerRunning) {
		fServerRunning = true;
		BMessage watch(BT_START_WATCHING_CONNECTIONS);
		watch.AddMessenger("messenger", BMessenger(this));
		BMessenger(BLUETOOTH_SIGNATURE).SendMessage(&watch, (BHandler*)NULL,
			1000000);
		_ProbeAdapters();
	} else if (!running && fServerRunning) {
		fServerRunning = false;
		fProbing = false;
		fProbeGeneration++;
		fAdapters.clear();
		fAdapterIndex = -1;
		ActiveLocalDevice = NULL;
		fScanner->SetAdapter(NULL);
		_ClearNearby();

		if (fOperation != OPERATION_NONE) {
			BString title(B_TRANSLATE("Could not connect to %name%"));
			title.ReplaceFirst("%name%", fOperationName);
			_ShowStatus(title, B_TRANSLATE("The Bluetooth service stopped."),
				false);
			_EndOperation();
		}

		if (fRestartPending)
			_StartServices();
	}

	_LoadPaired();
	_UpdateHeader();
	_UpdateScanning();
}


void
BluetoothWindow::_StartServices()
{
	fRestartPending = false;
	if (be_roster->IsRunning(BLUETOOTH_SIGNATURE))
		return;

	// The launch_daemon runs the server as a service; starting the job also
	// enables it again after _StopServices().
	if (BLaunchRoster().Start(kServerJob) == B_OK) {
		_ShowStatus(B_TRANSLATE("Starting the Bluetooth service"
			B_UTF8_ELLIPSIS), BString(), false);
		return;
	}

	// Without that job, a system may need to prepare its controller first
	// (for example load firmware); an optional user script does that and
	// starts the server.
	BPath hook;
	status_t error = find_directory(B_USER_SETTINGS_DIRECTORY, &hook);
	if (error == B_OK)
		error = hook.Append("bluetooth/start-services");
	if (error == B_OK && access(hook.Path(), R_OK) == 0) {
		ServiceHook* data = new ServiceHook;
		data->path = hook;
		data->target = BMessenger(this);
		thread_id thread = spawn_thread(RunServiceStartHook,
			"Bluetooth service startup", B_NORMAL_PRIORITY, data);
		if (thread >= 0) {
			_ShowStatus(B_TRANSLATE("Starting the Bluetooth service"
				B_UTF8_ELLIPSIS), BString(), false);
			resume_thread(thread);
			return;
		}
		delete data;
	}

	error = be_roster->Launch(BLUETOOTH_SIGNATURE);
	if (error != B_OK && error != B_ALREADY_RUNNING) {
		BString text(B_TRANSLATE("The Bluetooth service could not be "
			"started: %error%"));
		text.ReplaceFirst("%error%", strerror(error));
		_ShowStatus(B_TRANSLATE("Could not start the Bluetooth service"),
			text, false);
	}
}


void
BluetoothWindow::_StopServices()
{
	// The launch_daemon would start a quitting service right away again;
	// stopping its job keeps the server down until it is started.
	if (BLaunchRoster().Stop(kServerJob) == B_OK)
		return;

	if (be_roster->IsRunning(BLUETOOTH_SIGNATURE)) {
		BMessenger(BLUETOOTH_SIGNATURE).SendMessage(B_QUIT_REQUESTED,
			(BHandler*)NULL);
	}
}


void
BluetoothWindow::_ProbeAdapters()
{
	if (!fServerRunning)
		return;
	fProbing = true;
	fProbeStarted = system_time();
	if (ProbeAdapters(BMessenger(this), ++fProbeGeneration) != B_OK)
		fProbing = false;
	_UpdateHeader();
}


void
BluetoothWindow::_AdaptersProbed(BMessage* message)
{
	if (message->GetInt32("generation", -1) != fProbeGeneration)
		return;
	fProbing = false;

	int32 previousID = -1;
	if (const Adapter* current = _CurrentAdapter())
		previousID = current->id;

	fAdapters.clear();
	void* pointer;
	for (int32 i = 0; message->FindPointer("device", i, &pointer) == B_OK;
			i++) {
		Adapter adapter;
		adapter.device = (Bluetooth::LocalDevice*)pointer;
		adapter.id = message->GetInt32("id", i, -1);
		adapter.name = message->GetString("name", i, "");
		const void* address;
		ssize_t size;
		if (message->FindData("address", B_RAW_TYPE, i, &address, &size)
				!= B_OK || size != 6)
			continue;
		memcpy(adapter.address, address, 6);
		fAdapters.push_back(adapter);
	}

	while (BMenuItem* item = fAdapterMenu->RemoveItem((int32)0))
		delete item;

	BluetoothSettings settings;
	settings.LoadSettings();
	bdaddr_t picked = settings.PickedDevice();

	int32 selected = fAdapters.empty() ? -1 : 0;
	for (size_t i = 0; i < fAdapters.size(); i++) {
		BString label(fAdapters[i].name);
		if (label.IsEmpty())
			label = AddressString(fAdapters[i].address);
		BMessage* select = new BMessage(kMsgAdapterSelected);
		select->AddInt32("index", i);
		fAdapterMenu->AddItem(new BMenuItem(label, select));

		if (fAdapters[i].id == previousID
			|| (previousID < 0 && memcmp(picked.b, fAdapters[i].address, 6) == 0))
			selected = i;
	}
	fAdapterMenu->SetTargetForItems(this);

	bool showMenu = fAdapters.size() > 1;
	if (showMenu && fAdapterField->IsHidden(fAdapterField))
		fAdapterField->Show();
	else if (!showMenu && !fAdapterField->IsHidden(fAdapterField))
		fAdapterField->Hide();

	fAdapterIndex = -2;
		// forces _SelectAdapter to apply the choice
	_SelectAdapter(selected);
}


void
BluetoothWindow::_SelectAdapter(int32 index)
{
	if (fOperation != OPERATION_NONE)
		return;

	fAdapterIndex = index;
	const Adapter* adapter = _CurrentAdapter();
	ActiveLocalDevice = adapter != NULL ? adapter->device : NULL;

	if (BMenuItem* item = fAdapterMenu->ItemAt(index))
		item->SetMarked(true);

	if (adapter != NULL) {
		BluetoothSettings settings;
		settings.LoadSettings();
		bdaddr_t address;
		memcpy(address.b, adapter->address, 6);
		settings.SetPickedDevice(address);
		settings.SaveSettings();
	}

	_ClearNearby();
	fScanner->SetAdapter(ActiveLocalDevice);
	_UpdateScanning();
	_LoadPaired();
	_UpdateHeader();
	_UpdateNearbyButtons();
}


const BluetoothWindow::Adapter*
BluetoothWindow::_CurrentAdapter() const
{
	if (fAdapterIndex < 0 || fAdapterIndex >= (int32)fAdapters.size())
		return NULL;
	return &fAdapters[fAdapterIndex];
}


int32
BluetoothWindow::_HCIID() const
{
	const Adapter* adapter = _CurrentAdapter();
	return adapter != NULL ? adapter->id : -1;
}


void
BluetoothWindow::_UpdateHeader()
{
	const Adapter* adapter = _CurrentAdapter();
	BString title;
	BString subtitle;
	BString button;
	uint32 buttonMessage = 0;

	if (!fServerRunning) {
		title = B_TRANSLATE("Bluetooth service is not running");
		subtitle = B_TRANSLATE("Start the service to use Bluetooth devices.");
		button = B_TRANSLATE("Start service");
		buttonMessage = kMsgStartServices;
	} else if (fProbing && system_time() - fProbeStarted > kProbeTimeout) {
		title = B_TRANSLATE("Bluetooth service is not responding");
		subtitle = B_TRANSLATE("Restarting the service may help.");
		button = B_TRANSLATE("Restart service");
		buttonMessage = kMsgRestartServices;
	} else if (fProbing) {
		title = B_TRANSLATE("Looking for Bluetooth adapters" B_UTF8_ELLIPSIS);
	} else if (adapter == NULL) {
		title = B_TRANSLATE("No Bluetooth adapter found");
		subtitle = B_TRANSLATE("Connect a Bluetooth adapter, then check "
			"again.");
		button = B_TRANSLATE("Check again");
		buttonMessage = kMsgRefresh;
	} else {
		title = B_TRANSLATE("Bluetooth is on");
		subtitle = B_TRANSLATE_COMMENT("%name% (%address%)",
			"Local adapter name and address");
		subtitle.ReplaceFirst("%name%", adapter->name.IsEmpty()
			? B_TRANSLATE("This computer") : adapter->name.String());
		subtitle.ReplaceFirst("%address%", AddressString(adapter->address));
	}

	fTitleView->SetText(title);
	fSubtitleView->SetText(subtitle);
	fIconView->SetEnabled(adapter != NULL);

	if (buttonMessage != 0) {
		fServiceButton->SetLabel(button);
		fServiceButton->SetMessage(new BMessage(buttonMessage));
		if (fServiceButton->IsHidden(fServiceButton))
			fServiceButton->Show();
	} else if (!fServiceButton->IsHidden(fServiceButton))
		fServiceButton->Hide();

	fAdapterField->SetEnabled(fOperation == OPERATION_NONE);
	_UpdateScanStatus();
}


void
BluetoothWindow::_UpdateScanning()
{
	bool scan = fServerRunning && _CurrentAdapter() != NULL && !fMinimized;
	if (scan && !fScanner->IsRunning()) {
		fSearchingSince = system_time();
		fScanner->Start();
	} else if (!scan && fScanner->IsRunning())
		fScanner->Stop();
	_UpdateScanStatus();
}


//	#pragma mark - My devices


void
BluetoothWindow::_LoadPaired()
{
	LoadPairedDevices(BMessenger(this), ++fPairedGeneration,
		fServerRunning ? _HCIID() : -1);
}


void
BluetoothWindow::_PairedLoaded(BMessage* message)
{
	if (message->GetInt32("generation", -1) != fPairedGeneration)
		return;

	device_key selectedKey = 0;
	bool hadSelection = false;
	if (DeviceItem* item = fPairedList->SelectedDevice()) {
		selectedKey = item->Key();
		hadSelection = true;
	}

	_ClearPaired();

	BMessage entry;
	for (int32 i = 0; message->FindMessage("classic", i, &entry) == B_OK;
			i++) {
		const void* address;
		ssize_t size;
		if (entry.FindData("address", B_RAW_TYPE, &address, &size) != B_OK
			|| size != 6)
			continue;
		device_key key = KeyForAddress((const uint8*)address);
		if (fPaired.find(key) != fPaired.end())
			continue;

		PairedDevice* device = new PairedDevice(key);
		uint32 record = entry.GetUInt32("class", 0);
		uint8 bytes[3] = { (uint8)(record & 0xff),
			(uint8)((record >> 8) & 0xff), (uint8)((record >> 16) & 0xff) };
		device->deviceClass.SetRecord(bytes);
		device->serverName = entry.GetString("name", "");
		device->connectionState = entry.GetInt32("state",
			Bluetooth::RemoteDevice::DISCONNECTED);
		device->pageRepetitionMode = entry.GetUInt8("page_repetition_mode",
			0);
		device->clockOffset = entry.GetUInt16("clock_offset", 0);
		fNames.Lookup(key, device->knownName, device->knownKind);
		fPaired[key] = device;
	}

	const Adapter* adapter = _CurrentAdapter();
	for (int32 i = 0; message->FindMessage("le", i, &entry) == B_OK; i++) {
		const void* address;
		const void* localAddress;
		ssize_t size, localSize;
		if (entry.FindData("address", B_RAW_TYPE, &address, &size) != B_OK
			|| size != 6
			|| entry.FindData("local_address", B_RAW_TYPE, &localAddress,
				&localSize) != B_OK || localSize != 6)
			continue;
		if (adapter != NULL && memcmp(adapter->address, localAddress, 6) != 0)
			continue;
		device_key key = KeyForAddress((const uint8*)address);
		if (fPaired.find(key) != fPaired.end())
			continue;

		PairedDevice* device = new PairedDevice(key);
		device->isLE = true;
		device->leAddressType = entry.GetUInt8("address_type", 0);
		device->leMouse = entry.GetBool("mouse", false);
		device->leLocalAddressType = entry.GetUInt8("local_type", 0);
		memcpy(device->leLocalAddress, localAddress, 6);
		fNames.Lookup(key, device->knownName, device->knownKind);
		fPaired[key] = device;
	}

	// Names seen nearby fill in what the server and bond store lack.
	std::vector<PairedDevice*> sorted;
	std::map<device_key, PairedDevice*>::iterator iterator;
	for (iterator = fPaired.begin(); iterator != fPaired.end(); iterator++) {
		PairedDevice* device = iterator->second;
		std::map<device_key, NearbyDevice*>::iterator nearby
			= fNearby.find(device->key);
		if (device->knownName.IsEmpty() && nearby != fNearby.end()
			&& nearby->second->HasName()) {
			device->knownName = nearby->second->Name();
			device->knownKind = nearby->second->Kind();
			fNames.Set(device->key, device->knownName, device->knownKind);
		}
		sorted.push_back(device);
	}
	std::sort(sorted.begin(), sorted.end(),
		[](PairedDevice* a, PairedDevice* b) {
			return a->Name().ICompare(b->Name()) < 0;
		});

	for (size_t i = 0; i < sorted.size(); i++) {
		sorted[i]->item = new DeviceItem(sorted[i]->key);
		_UpdatePairedItem(sorted[i]);
		fPairedList->AddItem(sorted[i]->item);
		if (hadSelection && sorted[i]->key == selectedKey)
			fPairedList->Select(i);
	}

	fNames.Save();
	_UpdatePairedButtons();
	_UpdateAllNearby();
}


void
BluetoothWindow::_ClearPaired()
{
	fPairedList->MakeEmpty();
	std::map<device_key, PairedDevice*>::iterator iterator;
	for (iterator = fPaired.begin(); iterator != fPaired.end(); iterator++) {
		delete iterator->second->item;
		delete iterator->second;
	}
	fPaired.clear();
}


void
BluetoothWindow::_UpdatePairedItem(PairedDevice* device)
{
	if (device->item == NULL)
		return;

	device_kind kind = device->Kind();
	BString detail(KindLabel(kind));
	BString status;
	if (device->isLE) {
		detail << " · " << (device->leMouse
			? B_TRANSLATE("Connects automatically")
			: B_TRANSLATE("Low Energy"));
		status = B_TRANSLATE("Paired");
	} else {
		switch (device->connectionState) {
			case Bluetooth::RemoteDevice::CONNECTED:
				status = B_TRANSLATE("Connected");
				break;
			case Bluetooth::RemoteDevice::CONNECTING:
				status = B_TRANSLATE("Connecting" B_UTF8_ELLIPSIS);
				break;
			default:
				status = B_TRANSLATE("Not connected");
				break;
		}
	}
	if (fOperation != OPERATION_NONE && fOperationKey == device->key)
		detail = _OperationProgressText();

	if (device->item->SetContent(kind, device->Name(), detail, status, -1)) {
		int32 index = fPairedList->IndexOf(device->item);
		if (index >= 0)
			fPairedList->InvalidateItem(index);
	}
}


void
BluetoothWindow::_UpdatePairedButtons()
{
	PairedDevice* device = _SelectedPaired();
	bool idle = fOperation == OPERATION_NONE;
	bool classic = device != NULL && !device->isLE && fServerRunning
		&& _CurrentAdapter() != NULL;

	fConnectButton->SetEnabled(classic && idle
		&& device->connectionState == Bluetooth::RemoteDevice::DISCONNECTED);
	fDisconnectButton->SetEnabled(classic
		&& device->connectionState != Bluetooth::RemoteDevice::DISCONNECTED);
	fRemoveButton->SetEnabled(device != NULL
		&& (idle || fOperationKey != device->key));
}


PairedDevice*
BluetoothWindow::_SelectedPaired() const
{
	DeviceItem* item = fPairedList->SelectedDevice();
	if (item == NULL)
		return NULL;
	std::map<device_key, PairedDevice*>::const_iterator found
		= fPaired.find(item->Key());
	return found != fPaired.end() ? found->second : NULL;
}


void
BluetoothWindow::_ConnectPaired()
{
	PairedDevice* device = _SelectedPaired();
	if (device == NULL || device->isLE || fOperation != OPERATION_NONE
		|| device->connectionState != Bluetooth::RemoteDevice::DISCONNECTED
		|| _CurrentAdapter() == NULL)
		return;

	fOperationFromPaired = true;
	fOperationClass = device->deviceClass.Record();
	fOperationRepetitionMode = device->pageRepetitionMode;
	fOperationClockOffset = device->clockOffset;
	_BeginOperation(OPERATION_CLASSIC_CONNECT, device->key, device->Name(),
		device->Kind());
}


void
BluetoothWindow::_DisconnectPaired()
{
	PairedDevice* device = _SelectedPaired();
	if (device == NULL || device->isLE)
		return;

	bool connecting
		= device->connectionState == Bluetooth::RemoteDevice::CONNECTING;
	_ReleaseAudioSink(device->address);
	SendClassicRequest(connecting ? BT_REQ_CANCEL_CONN : BT_REQ_DISCONNECT,
		_HCIID(), device->address);
}


/*!	A speaker the user disconnects or forgets stops being the sound output;
	the output goes back to what it was before.
*/
void
BluetoothWindow::_ReleaseAudioSink(const uint8 address[6])
{
	bdaddr_t sink;
	BString name;
	if (Bluetooth::GetAudioSink(sink, name) == B_OK
		&& memcmp(sink.b, address, 6) == 0)
		Bluetooth::SetAudioSink(NULL, NULL);
}


void
BluetoothWindow::_RemovePaired()
{
	PairedDevice* device = _SelectedPaired();
	if (device == NULL)
		return;

	BString text(B_TRANSLATE("Remove \"%name%\"?\n\nIt has to be paired again "
		"before it can connect to this computer."));
	text.ReplaceFirst("%name%", device->Name());
	if (device->isLE && device->leMouse) {
		text << "\n\n" << B_TRANSLATE("The mouse stops working here until it "
			"is paired again.");
	}

	BAlert* alert = new BAlert(B_TRANSLATE("Remove device"), text,
		B_TRANSLATE("Cancel"), B_TRANSLATE("Remove"), NULL, B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	BMessage* confirmed = new BMessage(kMsgRemoveConfirmed);
	confirmed->AddUInt64("key", device->key);
	alert->Go(new BInvoker(confirmed, this));
}


void
BluetoothWindow::_ConnectionEvent(BMessage* message)
{
	const bdaddr_t* address = NULL;
	ssize_t size;
	if (message->FindData("bdaddr", B_ANY_TYPE, (const void**)&address,
			&size) != B_OK || size != sizeof(bdaddr_t))
		address = NULL;
	uint8 status = message->GetUInt8("status", BT_OK);
	device_key key = address != NULL ? KeyForAddress(address->b) : 0;

	if (fOperation == OPERATION_CLASSIC_CONNECT
		&& fOperationState == OPERATION_RUNNING
		&& (message->what == BT_MSG_CONN_COMPLETED
			|| message->what == BT_MSG_CONN_FAILED)
		&& (address == NULL || key == fOperationKey)) {
		_ClassicConnectFinished(message->what == BT_MSG_CONN_COMPLETED
			&& status == BT_OK, status);
	}

	std::map<device_key, PairedDevice*>::iterator found = fPaired.find(key);
	if (address == NULL || found == fPaired.end()
		|| message->what == BT_MSG_NEW_REMOTE_DEVICE) {
		_LoadPaired();
		return;
	}

	PairedDevice* device = found->second;
	if (message->what == BT_MSG_CONN_COMPLETED && status == BT_OK)
		device->connectionState = Bluetooth::RemoteDevice::CONNECTED;
	else if (message->what != BT_MSG_CONN_COMPLETED
		|| status != BT_OK)
		device->connectionState = Bluetooth::RemoteDevice::DISCONNECTED;
	_UpdatePairedItem(device);
	_UpdatePairedButtons();
}


void
BluetoothWindow::_LearnName(device_key key, const BString& name,
	device_kind kind)
{
	std::map<device_key, PairedDevice*>::iterator found = fPaired.find(key);
	if (found == fPaired.end())
		return;

	PairedDevice* device = found->second;
	if (device->knownName == name
		&& (kind == DEVICE_KIND_GENERIC || device->knownKind == kind))
		return;
	device->knownName = name;
	if (kind != DEVICE_KIND_GENERIC)
		device->knownKind = kind;
	fNames.Set(key, name, device->knownKind);
	_UpdatePairedItem(device);
}


//	#pragma mark - Nearby devices


NearbyDevice*
BluetoothWindow::_Nearby(const uint8 address[6])
{
	device_key key = KeyForAddress(address);
	std::map<device_key, NearbyDevice*>::iterator found = fNearby.find(key);
	if (found != fNearby.end())
		return found->second;

	NearbyDevice* device = new NearbyDevice(key);
	fNames.Lookup(key, device->cachedName, device->cachedKind);
	fNearby[key] = device;
	return device;
}


void
BluetoothWindow::_UpdateNearbyItem(NearbyDevice* device)
{
	bool operationDevice = fOperation != OPERATION_NONE
		&& fOperationKey == device->key && !fOperationFromPaired;
	bool twinListed = device->companionOf != 0
		&& fNearby.find(device->companionOf) != fNearby.end();
	const bool showAll = fShowUnnamed->Value() == B_CONTROL_ON;
	bool show = operationDevice
		|| (fPaired.find(device->key) == fPaired.end() && !twinListed
			&& (device->IsSupported() || showAll)
			&& (device->HasName() || device->IsHID() || showAll));

	if (!show) {
		if (device->item != NULL)
			fNearbyList->RemoveItem(device->item);
		return;
	}

	device_kind kind = device->Kind();
	BString detail;
	if (operationDevice)
		detail = _OperationProgressText();
	else {
		detail = KindLabel(kind);
		if (!device->IsSupported())
			detail << " · " << B_TRANSLATE("Not supported");
		else if (device->InPairingMode())
			detail << " · " << B_TRANSLATE("Pairing mode");
		else if (device->UsesLEPairing() && !device->connectable)
			detail << " · " << B_TRANSLATE("Not accepting connections");
	}

	if (device->item == NULL)
		device->item = new DeviceItem(device->key);
	bool changed = device->item->SetContent(kind, device->Name(), detail,
		BString(), device->hasRSSI ? SignalBars(device->rssi) : -1);

	int32 index = fNearbyList->IndexOf(device->item);
	if (index < 0) {
		fNearbyList->AddItem(device->item);
		_UpdateNearbyButtons();
	} else if (changed) {
		fNearbyList->InvalidateItem(index);
		if (device->item->IsSelected())
			_UpdateNearbyButtons();
	}
}


void
BluetoothWindow::_LinkCompanions(NearbyDevice* device)
{
	NearbyDevice* classic = NULL;
	NearbyDevice* le = NULL;
	int32 matches = 0;

	std::map<device_key, NearbyDevice*>::iterator iterator;
	for (iterator = fNearby.begin(); iterator != fNearby.end(); iterator++) {
		NearbyDevice* other = iterator->second;
		if (other == device)
			continue;
		if (other->IsCompanionOf(*device) && other->HasName()) {
			classic = device;
			le = other;
			matches++;
		} else if (device->IsCompanionOf(*other) && device->HasName()) {
			classic = other;
			le = device;
			matches++;
		}
	}

	// Only an unambiguous twin, and only for a Classic device that has no
	// name of its own.
	if (matches != 1 || !classic->classicName.IsEmpty()
		|| !classic->cachedName.IsEmpty())
		return;

	BString name = le->Name();
	if (classic->companionName == name && le->companionOf == classic->key)
		return;
	classic->companionName = name;
	le->companionOf = classic->key;
	if (le->hasRSSI) {
		classic->hasRSSI = true;
		classic->rssi = le->rssi;
	}
	if (device == le)
		_UpdateNearbyItem(classic);
	else
		_UpdateNearbyItem(le);
}


void
BluetoothWindow::_UpdateAllNearby()
{
	std::map<device_key, NearbyDevice*>::iterator iterator;
	for (iterator = fNearby.begin(); iterator != fNearby.end(); iterator++)
		_UpdateNearbyItem(iterator->second);
	_UpdateNearbyButtons();
}


void
BluetoothWindow::_ClearNearby()
{
	fNearbyList->MakeEmpty();
	std::map<device_key, NearbyDevice*>::iterator iterator;
	for (iterator = fNearby.begin(); iterator != fNearby.end(); iterator++) {
		delete iterator->second->item;
		delete iterator->second;
	}
	fNearby.clear();
	fNameLookupActive = false;
	_UpdateNearbyButtons();
}


void
BluetoothWindow::_ExpireNearby()
{
	// Only drop devices once the scanner has had time to see them again.
	bigtime_t now = system_time();
	if (!fScanner->IsSearching() || fSearchingSince == 0
		|| now - fSearchingSince < kExpireGrace)
		return;

	std::map<device_key, NearbyDevice*>::iterator iterator = fNearby.begin();
	while (iterator != fNearby.end()) {
		NearbyDevice* device = iterator->second;
		if (now - device->lastSeen < kExpireAfter
			|| (fOperation != OPERATION_NONE && fOperationKey == device->key)) {
			iterator++;
			continue;
		}
		if (device->item != NULL) {
			fNearbyList->RemoveItem(device->item);
			delete device->item;
		}
		delete device;
		fNearby.erase(iterator++);
	}
	_UpdateNearbyButtons();
}


void
BluetoothWindow::_UpdateNearbyButtons()
{
	NearbyDevice* device = _SelectedNearby();
	fPairButton->SetEnabled(device != NULL && device->CanConnect()
		&& fOperation == OPERATION_NONE && fServerRunning
		&& _CurrentAdapter() != NULL);
}


NearbyDevice*
BluetoothWindow::_SelectedNearby() const
{
	DeviceItem* item = fNearbyList->SelectedDevice();
	if (item == NULL)
		return NULL;
	std::map<device_key, NearbyDevice*>::const_iterator found
		= fNearby.find(item->Key());
	return found != fNearby.end() ? found->second : NULL;
}


void
BluetoothWindow::_UpdateScanStatus()
{
	if (fScanner->IsSearching() && fSearchingSince == 0)
		fSearchingSince = system_time();
	else if (!fScanner->IsSearching())
		fSearchingSince = 0;

	fScanStatusView->SetText(fScanner->StatusText());

	if (!fServerRunning || _CurrentAdapter() == NULL)
		fNearbyList->SetEmptyText(B_TRANSLATE("Bluetooth is not available"));
	else if (fScanner->IsSearching())
		fNearbyList->SetEmptyText(B_TRANSLATE("Looking for devices"
			B_UTF8_ELLIPSIS));
	else
		fNearbyList->SetEmptyText(B_TRANSLATE("No devices found"));
}


void
BluetoothWindow::_StartNameLookup()
{
	if (fNameLookupActive || !fServerRunning || _HCIID() < 0
		|| fOperation != OPERATION_NONE || fScanner->IsPaused())
		return;

	std::map<device_key, NearbyDevice*>::iterator iterator;
	for (iterator = fNearby.begin(); iterator != fNearby.end(); iterator++) {
		NearbyDevice* device = iterator->second;
		if (!device->NeedsNameLookup())
			continue;
		// One attempt per device; a failed request is not retried.
		device->nameLookupDone = true;
		if (LookupRemoteName(BMessenger(this), _HCIID(), device->address,
				device->pageRepetitionMode, device->clockOffset) == B_OK) {
			fNameLookupActive = true;
			fNameLookupStarted = system_time();
		}
		return;
	}
}


void
BluetoothWindow::_NameResult(BMessage* message)
{
	fNameLookupActive = false;

	device_key key = message->GetUInt64("key", 0);
	std::map<device_key, NearbyDevice*>::iterator found = fNearby.find(key);
	const char* name;
	bool hasName = message->FindString("name", &name) == B_OK;
	TRACE("remote name of %s: %s", found != fNearby.end()
			? AddressString(found->second->address).String() : "?",
		hasName ? name : "(request failed)");
	if (found != fNearby.end() && hasName) {
		NearbyDevice* device = found->second;
		device->SetClassicName(name);
		if (device->HasName())
			_LearnName(key, device->Name(), device->Kind());
		_UpdateNearbyItem(device);
	}

	_StartNameLookup();
}


//	#pragma mark - Connecting and pairing


void
BluetoothWindow::_ConnectNearby()
{
	NearbyDevice* device = _SelectedNearby();
	if (device == NULL || !device->CanConnect()
		|| fOperation != OPERATION_NONE || _CurrentAdapter() == NULL)
		return;

	fOperationFromPaired = false;
	fOperationAddressType = device->leAddressType;
	fOperationClass = device->deviceClass.Record();
	fOperationRepetitionMode = device->pageRepetitionMode;
	fOperationClockOffset = device->clockOffset;
	_BeginOperation(device->UsesLEPairing()
			? OPERATION_LE_PAIR : OPERATION_CLASSIC_CONNECT,
		device->key, device->Name(), device->Kind());
}


void
BluetoothWindow::_BeginOperation(operation_type type, device_key key,
	const BString& name, device_kind kind)
{
	fOperation = type;
	fOperationState = OPERATION_WAITING;
	fOperationKey = key;
	fOperationName = name;
	fOperationKind = kind;
	fOperationStage = -1;
	fOperationDetail = B_TRANSLATE("Pausing the device search"
		B_UTF8_ELLIPSIS);
	fOperationStarted = system_time();

	BString title(type == OPERATION_LE_PAIR
		? B_TRANSLATE("Pairing with %name%" B_UTF8_ELLIPSIS)
		: B_TRANSLATE("Connecting to %name%" B_UTF8_ELLIPSIS));
	title.ReplaceFirst("%name%", name);
	_ShowStatus(title, fOperationDetail, false);

	_UpdateHeader();
	_UpdatePairedButtons();
	_UpdateNearbyButtons();
	_UpdateAllNearby();
	if (fPaired.find(key) != fPaired.end())
		_UpdatePairedItem(fPaired[key]);

	// A Classic inquiry or our LE scan would compete with the connection.
	// ScannerPaused() continues in _RunOperation().
	fScanner->Pause();
}


void
BluetoothWindow::_RunOperation()
{
	const Adapter* adapter = _CurrentAdapter();
	if (adapter == NULL) {
		BString title(B_TRANSLATE("Could not connect to %name%"));
		title.ReplaceFirst("%name%", fOperationName);
		_ShowStatus(title, B_TRANSLATE("No Bluetooth adapter is available."),
			false);
		_EndOperation();
		return;
	}

	fOperationState = OPERATION_RUNNING;
	fOperationStarted = system_time();

	uint8 address[6];
	AddressForKey(fOperationKey, address);
	TRACE("%s %s (%s)", fOperation == OPERATION_LE_PAIR
			? "pairing with" : "connecting to", fOperationName.String(),
		AddressString(address).String());

	status_t status;
	if (fOperation == OPERATION_LE_PAIR) {
		fOperationStage = Bluetooth::LE_PAIRING_CONNECT;
		fOperationDetail = "";
		status = PairLEDeviceAsync(BMessenger(this), adapter->id,
			adapter->address, address, fOperationAddressType);
	} else {
		fOperationDetail = B_TRANSLATE("Requesting a connection"
			B_UTF8_ELLIPSIS);
		// The server stores the name sent with the request; only send a
		// real one, not a placeholder.
		std::map<device_key, NearbyDevice*>::iterator nearby
			= fNearby.find(fOperationKey);
		BString name;
		if (nearby != fNearby.end() && nearby->second->HasName())
			name = nearby->second->Name();
		else if (fOperationFromPaired && fPaired.find(fOperationKey)
				!= fPaired.end())
			SanitizeName(fPaired[fOperationKey]->Name(), address, name);
		if (!name.IsEmpty() && !fOperationFromPaired) {
			fNames.Set(fOperationKey, name, fOperationKind);
			fNames.Save();
		}
		status = ConnectClassic(BMessenger(this), adapter->id, address, name,
			fOperationClass, fOperationRepetitionMode, fOperationClockOffset);
	}

	if (status != B_OK) {
		BString title(B_TRANSLATE("Could not connect to %name%"));
		title.ReplaceFirst("%name%", fOperationName);
		_ShowStatus(title, strerror(status), false);
		_EndOperation();
		return;
	}

	fStatusText->SetText(_OperationProgressText());
	_UpdateAllNearby();
}


void
BluetoothWindow::_PairingProgress(BMessage* message)
{
	if (fOperation != OPERATION_LE_PAIR
		|| fOperationState != OPERATION_RUNNING)
		return;

	if (message->what == kMsgLEPairBusy) {
		fOperationStage = Bluetooth::LE_PAIRING_CONNECT;
		fOperationDetail = B_TRANSLATE("The Bluetooth controller is busy, "
			"trying again (attempt %attempt% of %attempts%)");
		fOperationDetail.ReplaceFirst("%attempt%",
			BString() << message->GetInt32("attempt", 0));
		fOperationDetail.ReplaceFirst("%attempts%",
			BString() << message->GetInt32("attempts", 0));
	} else {
		int32 stage;
		if (message->FindInt32("stage", &stage) != B_OK)
			return;
		fOperationStage = stage;
		fOperationDetail = message->GetString("detail", "");
	}

	fStatusText->SetText(_OperationProgressText());
	std::map<device_key, NearbyDevice*>::iterator nearby
		= fNearby.find(fOperationKey);
	if (nearby != fNearby.end())
		_UpdateNearbyItem(nearby->second);
}


void
BluetoothWindow::_LEPairDone(BMessage* message)
{
	if (fOperation != OPERATION_LE_PAIR
		|| message->GetUInt64("key", 0) != fOperationKey)
		return;

	status_t status = message->GetInt32("status", B_ERROR);
	int32 stage = message->GetInt32("stage", Bluetooth::LE_PAIRING_CONNECT);
	bool bonded = message->GetBool("bonded", false);
	bool mouseReady = message->GetBool("hid_mouse_ready", false);
	status_t hidStatus = message->GetInt32("hid_status", B_OK);
	BString detail = message->GetString("detail", "");
	TRACE("pairing with %s finished: %s at stage %" B_PRId32
		", bonded %d, mouse %d: %s", fOperationName.String(), strerror(status),
		stage, bonded, mouseReady, detail.String());

	BString title;
	BString text;
	bool showLog = true;

	if (status == B_OK) {
		// Remember the advertised name; the bond store has only addresses.
		std::map<device_key, NearbyDevice*>::iterator nearby
			= fNearby.find(fOperationKey);
		if (nearby != fNearby.end() && nearby->second->HasName()) {
			fNames.Set(fOperationKey, nearby->second->Name(),
				mouseReady ? DEVICE_KIND_MOUSE : fOperationKind);
			fNames.Save();
		}

		if (mouseReady) {
			title = B_TRANSLATE("Paired with %name%");
			text = B_TRANSLATE("The mouse is ready. It connects automatically "
				"when you use it.");
			showLog = false;
		} else if (bonded) {
			title = B_TRANSLATE("Paired with %name%");
			text = B_TRANSLATE("Paired, but no mouse input report was found "
				"(%error%).");
			text.ReplaceFirst("%error%", strerror(hidStatus));
		} else {
			title = B_TRANSLATE("Connected to %name%");
			text = B_TRANSLATE("The connection was encrypted, but the device "
				"did not provide a pairing key, so it cannot reconnect "
				"automatically.");
		}
		if (!detail.IsEmpty() && !mouseReady)
			text << "\n" << detail;
	} else {
		title = B_TRANSLATE("Could not pair with %name%");
		text = B_TRANSLATE("Failed while: %stage%.");
		text.ReplaceFirst("%stage%", StageText(stage));
		text << "\n" << (detail.IsEmpty() ? BString(strerror(status)) : detail);
		if (IsInputKind(fOperationKind))
			text << "\n\n" << PairingModeHint();
	}
	title.ReplaceFirst("%name%", fOperationName);

	_ShowStatus(title, text, showLog);
	_EndOperation();
	_LoadPaired();
}


void
BluetoothWindow::_ClassicConnectSent(BMessage* message)
{
	if (fOperation != OPERATION_CLASSIC_CONNECT
		|| message->GetUInt64("key", 0) != fOperationKey)
		return;

	status_t status = message->GetInt32("status", B_ERROR);
	if (status != B_OK) {
		BString title(B_TRANSLATE("Could not connect to %name%"));
		title.ReplaceFirst("%name%", fOperationName);
		BString text(B_TRANSLATE("The Bluetooth service did not accept the "
			"request: %error%"));
		text.ReplaceFirst("%error%", strerror(status));
		_ShowStatus(title, text, false);
		_EndOperation();
		return;
	}

	fOperationDetail = B_TRANSLATE("Waiting for the device. Accept the "
		"pairing request or enter the PIN if asked.");
	fStatusText->SetText(_OperationProgressText());
	std::map<device_key, NearbyDevice*>::iterator nearby
		= fNearby.find(fOperationKey);
	if (nearby != fNearby.end())
		_UpdateNearbyItem(nearby->second);
}


void
BluetoothWindow::_ClassicConnectFinished(bool success, uint8 status)
{
	if (fOperation != OPERATION_CLASSIC_CONNECT)
		return;

	BString title;
	BString text;
	if (success) {
		title = B_TRANSLATE("Connected to %name%");
		// Speakers and headsets say so in their class; a computer that
		// plays sound (an A2DP sink) has the Rendering and Audio service
		// bits (Assigned Numbers, Class of Device, bits 18 and 21).
		const uint32 renderingAudio = (1 << 18) | (1 << 21);
		if (fOperationKind == DEVICE_KIND_AUDIO
			|| (fOperationClass & renderingAudio) == renderingAudio) {
			// Speakers and headsets take over the sound output; the
			// Bluetooth audio output connects its stream to them.
			bdaddr_t address;
			AddressForKey(fOperationKey, address.b);
			if (Bluetooth::SetAudioSink(&address, fOperationName.String())
					== B_OK) {
				text = B_TRANSLATE("Sound now plays through it.");
			}
		}
	} else {
		title = B_TRANSLATE("Could not connect to %name%");
		if (status == 0)
			text = B_TRANSLATE("The device did not respond.");
		else {
			text = B_TRANSLATE("The device did not accept the connection "
				"(Bluetooth error 0x%code%).");
			char code[8];
			snprintf(code, sizeof(code), "%02x", status);
			text.ReplaceFirst("%code%", code);
		}
		text << " " << B_TRANSLATE("Make sure it is switched on and "
			"discoverable.");
	}
	title.ReplaceFirst("%name%", fOperationName);

	_ShowStatus(title, text, false);
	_EndOperation();
	_LoadPaired();
}


void
BluetoothWindow::_EndOperation()
{
	device_key key = fOperationKey;
	// A Classic device that just connected still has its service query
	// and, for a keyboard, its HID channels ahead; an inquiry now would
	// starve them.
	if (fOperation == OPERATION_CLASSIC_CONNECT)
		fScanner->HoldInquiry(30000000);
	fOperation = OPERATION_NONE;
	fOperationState = OPERATION_IDLE;
	fOperationStage = -1;
	fOperationDetail = "";

	fDismissButton->SetEnabled(true);
	fScanner->Resume();

	std::map<device_key, NearbyDevice*>::iterator nearby = fNearby.find(key);
	if (nearby != fNearby.end())
		_UpdateNearbyItem(nearby->second);
	std::map<device_key, PairedDevice*>::iterator paired = fPaired.find(key);
	if (paired != fPaired.end())
		_UpdatePairedItem(paired->second);

	_UpdateHeader();
	_UpdatePairedButtons();
	_UpdateNearbyButtons();
}


BString
BluetoothWindow::_OperationProgressText() const
{
	BString text;
	if (fOperation == OPERATION_LE_PAIR && fOperationState == OPERATION_RUNNING
		&& fOperationStage >= 0) {
		text = StageText(fOperationStage);
		text << B_UTF8_ELLIPSIS;
		if (!fOperationDetail.IsEmpty())
			text << " " << fOperationDetail;
	} else
		text = fOperationDetail;
	return text;
}


void
BluetoothWindow::_ShowStatus(const BString& title, const BString& text,
	bool showLog)
{
	fStatusTitle->SetText(title);
	fStatusText->SetText(text);

	if (showLog && fShowLogButton->IsHidden(fShowLogButton))
		fShowLogButton->Show();
	else if (!showLog && !fShowLogButton->IsHidden(fShowLogButton))
		fShowLogButton->Hide();

	fDismissButton->SetEnabled(fOperation == OPERATION_NONE);
	if (fStatusView->IsHidden(fStatusView))
		fStatusView->Show();
}


void
BluetoothWindow::_HideStatus()
{
	if (!fStatusView->IsHidden(fStatusView))
		fStatusView->Hide();
}


void
BluetoothWindow::_OpenAdvanced()
{
	if (fAdvancedWindow.IsValid()) {
		fAdvancedWindow.SendMessage(kMsgShowAdvanced);
		return;
	}

	AdvancedWindow* window = new AdvancedWindow(BMessenger(this),
		ActiveLocalDevice);
	fAdvancedWindow = BMessenger(window);
	window->Show();
}
