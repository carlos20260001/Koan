/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "WiFiWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <IconView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Roster.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <TextControl.h>

#include "JoinOtherWindow.h"
#include "NetworkListView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WiFiWindow"

#define QUOTED_NAME B_UTF8_OPEN_QUOTE "%name%" B_UTF8_CLOSE_QUOTE


static const uint32 kMsgPower = 'wpow';
static const uint32 kMsgDevice = 'wdev';
static const uint32 kMsgDisconnect = 'wdis';
static const uint32 kMsgKnownSelected = 'wkns';
static const uint32 kMsgOtherSelected = 'wots';
static const uint32 kMsgInvoke = 'winv';
static const uint32 kMsgJoinSelected = 'wjsl';
static const uint32 kMsgForget = 'wfgt';
static const uint32 kMsgForgetConfirmed = 'wfgc';
static const uint32 kMsgShowPassword = 'wshp';
static const uint32 kMsgPromptJoin = 'wpjn';
static const uint32 kMsgPromptCancel = 'wpcn';
static const uint32 kMsgPasswordModified = 'wpmd';
static const uint32 kMsgAdvanced = 'wadv';

static const bigtime_t kPanelScanInterval = 30000000;
static const bigtime_t kRequestLifetime = 20000000;


static BStringView*
MakeHeading(const char* name, const char* label)
{
	BStringView* view = new BStringView(name, label);
	BFont font(be_bold_font);
	view->SetFont(&font);
	return view;
}


WiFiWindow::WiFiWindow()
	:
	BWindow(BRect(0, 0, 440, 560), B_TRANSLATE_SYSTEM_NAME("WiFi"),
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE),
	fController(NULL),
	fHasState(false),
	fShowingNetServerError(false),
	fPromptAuthentication(B_NETWORK_AUTHENTICATION_NONE),
	fRequestedAuthentication(B_NETWORK_AUTHENTICATION_NONE),
	fRequestedTime(0)
{
	IconView* icon = new IconView(B_LARGE_ICON);
	size_t iconSize;
	const uint8* iconData = (const uint8*)be_app->AppResources()->LoadResource(
		B_VECTOR_ICON_TYPE, "BEOS:ICON", &iconSize);
	if (iconData != NULL)
		icon->SetIcon(iconData, iconSize);

	fPowerCheck = new BCheckBox("power", B_TRANSLATE("Wi-Fi"),
		new BMessage(kMsgPower));
	BFont bold(be_bold_font);
	fPowerCheck->SetFont(&bold);
	fPowerCheck->SetEnabled(false);

	fDeviceMenu = new BPopUpMenu("adapters");
	fDeviceField = new BMenuField("adapter", B_TRANSLATE("Adapter:"),
		fDeviceMenu);

	fCurrentSignal = new SignalView("signal");
	fCurrentName = new BStringView("current name",
		B_TRANSLATE("Looking for Wi-Fi adapters" B_UTF8_ELLIPSIS));
	fCurrentName->SetFont(&bold);
	fCurrentName->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fCurrentStatus = new BStringView("current status", "");
	fCurrentStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fDisconnectButton = new BButton("disconnect", B_TRANSLATE("Disconnect"),
		new BMessage(kMsgDisconnect));
	fDisconnectButton->SetEnabled(false);

	fKnownLabel = MakeHeading("known label", B_TRANSLATE("Known networks"));
	fKnownList = new NetworkListView("known networks");
	fKnownList->SetSelectionMessage(new BMessage(kMsgKnownSelected));
	fKnownList->SetInvocationMessage(new BMessage(kMsgInvoke));
	fForgetButton = new BButton("forget",
		B_TRANSLATE("Forget" B_UTF8_ELLIPSIS), new BMessage(kMsgForget));
	fForgetButton->SetEnabled(false);

	fOtherLabel = MakeHeading("other label", B_TRANSLATE("Other networks"));
	fScanLabel = new BStringView("scanning", "");
	fScanLabel->SetHighUIColor(B_PANEL_TEXT_COLOR, B_LIGHTEN_1_TINT);
	fOtherList = new NetworkListView("other networks");
	fOtherList->SetSelectionMessage(new BMessage(kMsgOtherSelected));
	fOtherList->SetInvocationMessage(new BMessage(kMsgInvoke));

	BScrollView* knownScroll = new BScrollView("known scroll", fKnownList,
		0, false, true);
	knownScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		be_plain_font->Size() * 6));
	BScrollView* otherScroll = new BScrollView("other scroll", fOtherList,
		0, false, true);
	otherScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		be_plain_font->Size() * 10));

	// Inline password prompt

	fPromptBox = new BBox("prompt");
	fPromptLabel = new BStringView("prompt label", "");
	fPromptLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fPasswordControl = new BTextControl("password", B_TRANSLATE("Password:"),
		"", new BMessage(kMsgPromptJoin));
	fPasswordControl->TextView()->HideTyping(true);
	fPasswordControl->SetModificationMessage(
		new BMessage(kMsgPasswordModified));
	fShowPassword = new BCheckBox("show password",
		B_TRANSLATE("Show password"), new BMessage(kMsgShowPassword));
	fRememberCheck = new BCheckBox("remember",
		B_TRANSLATE("Remember this network"), NULL);
	fRememberCheck->SetValue(B_CONTROL_ON);
	fPromptError = new BStringView("prompt error", "");
	fPromptError->SetHighUIColor(B_FAILURE_COLOR);
	fPromptError->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fPromptJoinButton = new BButton("prompt join", B_TRANSLATE("Join"),
		new BMessage(kMsgPromptJoin));
	BButton* promptCancel = new BButton("prompt cancel",
		B_TRANSLATE("Cancel"), new BMessage(kMsgPromptCancel));

	BLayoutBuilder::Group<>(fPromptBox, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fPromptLabel)
		.Add(fPasswordControl)
		.AddGroup(B_HORIZONTAL)
			.Add(fShowPassword)
			.Add(fRememberCheck)
			.AddGlue()
		.End()
		.Add(fPromptError)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(promptCancel)
			.Add(fPromptJoinButton)
		.End();

	fMessageView = new BStringView("message", "");
	fMessageView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BButton* joinOther = new BButton("join other",
		B_TRANSLATE("Join other network" B_UTF8_ELLIPSIS),
		new BMessage(kMsgWiFiJoinOther));
	BButton* advanced = new BButton("advanced",
		B_TRANSLATE("Advanced" B_UTF8_ELLIPSIS), new BMessage(kMsgAdvanced));
	fJoinButton = new BButton("join", B_TRANSLATE("Join"),
		new BMessage(kMsgJoinSelected));
	fJoinButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(icon)
			.Add(fPowerCheck)
			.AddGlue()
			.Add(fDeviceField)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fCurrentSignal)
			.AddGroup(B_VERTICAL, 0)
				.Add(fCurrentName)
				.Add(fCurrentStatus)
			.End()
			.Add(fDisconnectButton)
		.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL)
			.Add(fKnownLabel)
			.AddGlue()
			.Add(fForgetButton)
		.End()
		.Add(knownScroll, 1)
		.AddGroup(B_HORIZONTAL)
			.Add(fOtherLabel)
			.AddGlue()
			.Add(fScanLabel)
		.End()
		.Add(otherScroll, 3)
		.Add(fPromptBox)
		.Add(fMessageView)
		.AddGroup(B_HORIZONTAL)
			.Add(joinOther)
			.Add(advanced)
			.AddGlue()
			.Add(fJoinButton)
		.End();

	fPromptBox->Hide();
	fDeviceField->Hide();
	SetDefaultButton(fJoinButton);
	SetPulseRate(350000);
	CenterOnScreen();

	fController = new WiFiController(BMessenger(this), "panel",
		kPanelScanInterval);
	if (fController->Start() == B_OK)
		fControllerMessenger = BMessenger(fController);
	else {
		delete fController;
		fController = NULL;
		_SetMessage(B_TRANSLATE("Could not start the Wi-Fi monitor."), true);
	}
}


WiFiWindow::~WiFiWindow()
{
	if (fControllerMessenger.IsValid())
		fControllerMessenger.SendMessage(B_QUIT_REQUESTED);
}


bool
WiFiWindow::QuitRequested()
{
	// Do not keep a typed password around
	fPasswordControl->SetText("");
	return true;
}


void
WiFiWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgWiFiStateChanged:
		{
			WiFiState state;
			if (state.Unarchive(*message) == B_OK)
				_SetState(state);
			break;
		}

		case kMsgWiFiJoinResult:
			_JoinResult(message);
			break;

		case kMsgPower:
		{
			BMessage power(kMsgWiFiSetPower);
			power.AddBool("on", fPowerCheck->Value() == B_CONTROL_ON);
			fControllerMessenger.SendMessage(&power);
			if (fPowerCheck->Value() != B_CONTROL_ON)
				_HidePrompt();
			break;
		}

		case kMsgDevice:
		{
			const char* device;
			if (message->FindString("device", &device) == B_OK) {
				_HidePrompt();
				BMessage select(kMsgWiFiSelectDevice);
				select.AddString("device", device);
				fControllerMessenger.SendMessage(&select);
			}
			break;
		}

		case kMsgDisconnect:
			_HidePrompt();
			fControllerMessenger.SendMessage(kMsgWiFiLeave);
			break;

		case kMsgKnownSelected:
		case kMsgOtherSelected:
		{
			NetworkListView* list = message->what == kMsgKnownSelected
				? fKnownList : fOtherList;
			NetworkListView* other = list == fKnownList
				? fOtherList : fKnownList;
			const WiFiNetworkInfo* selected = list->SelectedNetwork();
			if (selected != NULL) {
				other->DeselectAll();
				if (fPromptBox->IsHidden() == false
					&& selected->name != fPromptName) {
					_HidePrompt();
				}
			}
			_UpdateButtons();
			break;
		}

		case kMsgInvoke:
		case kMsgJoinSelected:
			_JoinSelected();
			break;

		case kMsgForget:
			_Forget();
			break;

		case kMsgForgetConfirmed:
		{
			const char* name;
			if (message->GetInt32("which", 1) == 0
				&& message->FindString("name", &name) == B_OK) {
				BMessage forget(kMsgWiFiForget);
				forget.AddString("name", name);
				fControllerMessenger.SendMessage(&forget);
			}
			break;
		}

		case kMsgShowPassword:
		{
			BString text(fPasswordControl->Text());
			fPasswordControl->TextView()->HideTyping(
				fShowPassword->Value() != B_CONTROL_ON);
			fPasswordControl->SetText(text);
			text.SetTo('\0', text.Length());
			break;
		}

		case kMsgPasswordModified:
			if (fJoiningName != fPromptName)
				fPromptError->SetText("");
			break;

		case kMsgPromptJoin:
			if (!fPromptBox->IsHidden())
				_JoinFromPrompt();
			break;

		case kMsgPromptCancel:
			_HidePrompt();
			break;

		case kMsgAdvanced:
			be_roster->Launch("application/x-vnd.vos-Network");
			break;

		case kMsgWiFiJoinOther:
			_ShowJoinOther();
			break;

		case kMsgWiFiSelectNetwork:
		{
			// Sent by the WiFiStatus applet for a network needing a password
			const char* name;
			if (message->FindString("name", &name) != B_OK)
				break;
			const char* device;
			if (message->FindString("device", &device) == B_OK
				&& fState.device != device) {
				BMessage select(kMsgWiFiSelectDevice);
				select.AddString("device", device);
				fControllerMessenger.SendMessage(&select);
			}
			uint32 authentication = message->GetUInt32("authentication",
				B_NETWORK_AUTHENTICATION_WPA2);
			fRequestedName = name;
			fRequestedAuthentication = authentication;
			fRequestedTime = system_time();
			if (!fOtherList->Select(name))
				fKnownList->Select(name);
			_ShowPrompt(name, authentication);
			const char* error;
			if (message->FindString("error", &error) == B_OK)
				fPromptError->SetText(error);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
WiFiWindow::_SetState(const WiFiState& state)
{
	fState = state;
	fHasState = true;

	bool available = state.state != WIFI_STATE_NO_ADAPTER;
	bool on = available && state.state != WIFI_STATE_OFF;
	fPowerCheck->SetEnabled(available);
	fPowerCheck->SetValue(on ? B_CONTROL_ON : B_CONTROL_OFF);

	_UpdateDevices();
	_UpdateCurrent();

	std::vector<WiFiNetworkInfo> known;
	std::vector<WiFiNetworkInfo> other;
	for (size_t i = 0; i < state.networks.size(); i++) {
		// The current network is shown at the top, and among the known
		// networks if it is saved, so that it can be forgotten.
		const WiFiNetworkInfo& info = state.networks[i];
		if (info.saved)
			known.push_back(info);
		else if (info.inRange && (info.name != state.current || !on))
			other.push_back(info);
	}
	fKnownList->SetShowAvailability(on);
	fKnownList->SetNetworks(known);
	fOtherList->SetNetworks(other);

	if (!fRequestedName.IsEmpty()) {
		if (system_time() - fRequestedTime > kRequestLifetime)
			fRequestedName = "";
		else if (fOtherList->SelectedNetwork() == NULL
			&& fKnownList->SelectedNetwork() == NULL) {
			if (fOtherList->Select(fRequestedName)
				|| fKnownList->Select(fRequestedName))
				fRequestedName = "";
		}
	}

	fScanLabel->SetText(state.scanning && on
		? B_TRANSLATE("Scanning" B_UTF8_ELLIPSIS) : "");

	if (!state.netServerRunning && !fShowingNetServerError) {
		_SetMessage(B_TRANSLATE("The network service is not responding; "
			"saved networks are unavailable."), true);
		fShowingNetServerError = true;
	} else if (state.netServerRunning && fShowingNetServerError) {
		_SetMessage("", false);
		fShowingNetServerError = false;
	}

	if (!on && !fPromptBox->IsHidden())
		_HidePrompt();

	_UpdateButtons();
}


void
WiFiWindow::_UpdateCurrent()
{
	BString name;
	BString status;
	int32 bars = 0;
	bool enabled = false;
	bool animate = false;

	const WiFiNetworkInfo* current = fState.FindNetwork(fState.current);
	if (current != NULL && current->inRange)
		bars = current->bars;

	switch (fState.state) {
		case WIFI_STATE_NO_ADAPTER:
			name = B_TRANSLATE("No Wi-Fi adapter found");
			break;
		case WIFI_STATE_OFF:
			name = B_TRANSLATE("Wi-Fi is off");
			status = B_TRANSLATE("Turn Wi-Fi on to see nearby networks.");
			break;
		case WIFI_STATE_DISCONNECTED:
			name = B_TRANSLATE("Not connected");
			status = B_TRANSLATE("Choose a network to join.");
			break;
		case WIFI_STATE_CONNECTING:
			name = fState.current;
			status = B_TRANSLATE("Connecting" B_UTF8_ELLIPSIS);
			animate = true;
			break;
		case WIFI_STATE_OBTAINING_ADDRESS:
			name = fState.current;
			status = B_TRANSLATE("Connected, obtaining an IP address"
				B_UTF8_ELLIPSIS);
			enabled = true;
			break;
		case WIFI_STATE_NO_INTERNET:
			name = fState.current;
			status = B_TRANSLATE("Connected, no Internet (%address%)");
			status.ReplaceFirst("%address%", fState.address);
			enabled = true;
			break;
		case WIFI_STATE_CONNECTED:
			name = fState.current;
			status = B_TRANSLATE("Connected (%address%)");
			status.ReplaceFirst("%address%", fState.address);
			enabled = true;
			break;
	}

	fCurrentName->SetText(name);
	fCurrentStatus->SetText(status);
	fCurrentSignal->SetSignal(bars, enabled, animate);
	if (current != NULL && current->inRange)
		fCurrentSignal->SetToolTip(NetworkToolTip(*current));
	else
		fCurrentSignal->SetToolTip((const char*)NULL);

	fDisconnectButton->SetEnabled(fState.IsAssociated()
		|| fState.state == WIFI_STATE_CONNECTING);
}


void
WiFiWindow::_UpdateDevices()
{
	bool changed = fDeviceMenu->CountItems() != (int32)fState.devices.size();
	for (int32 i = 0; !changed && i < fDeviceMenu->CountItems(); i++) {
		if (fState.devices[i] != fDeviceMenu->ItemAt(i)->Label())
			changed = true;
	}

	if (changed) {
		fDeviceMenu->RemoveItems(0, fDeviceMenu->CountItems(), true);
		for (size_t i = 0; i < fState.devices.size(); i++) {
			BMessage* message = new BMessage(kMsgDevice);
			message->AddString("device", fState.devices[i]);
			fDeviceMenu->AddItem(new BMenuItem(fState.devices[i], message));
		}
		fDeviceMenu->SetTargetForItems(this);
	}

	BMenuItem* item = fDeviceMenu->FindItem(fState.device);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	bool show = fState.devices.size() > 1;
	if (show && fDeviceField->IsHidden())
		fDeviceField->Show();
	else if (!show && !fDeviceField->IsHidden())
		fDeviceField->Hide();
}


void
WiFiWindow::_UpdateButtons()
{
	const WiFiNetworkInfo* selected = _SelectedNetwork();
	bool on = fState.state != WIFI_STATE_NO_ADAPTER
		&& fState.state != WIFI_STATE_OFF;
	bool isCurrent = selected != NULL && selected->name == fState.current;
	fJoinButton->SetEnabled(on && selected != NULL && selected->inRange
		&& !isCurrent);
	fForgetButton->SetEnabled(fKnownList->SelectedNetwork() != NULL);
	fPromptJoinButton->SetEnabled(on && fJoiningName != fPromptName);
}


void
WiFiWindow::_JoinResult(BMessage* message)
{
	const char* name = message->GetString("name", "");
	status_t status = message->GetInt32("status", B_ERROR);
	const char* error = message->GetString("error", NULL);

	if (fJoiningName == name)
		fJoiningName = "";

	if (status == B_OK) {
		if (fPromptName == name)
			_HidePrompt();
		_SetMessage(error != NULL ? error : "", error != NULL);
	} else if (status == B_CANCELED) {
		if (error != NULL)
			_SetMessage(error, false);
	} else {
		const WiFiNetworkInfo* info = fState.FindNetwork(name);
		if (fPromptBox->IsHidden() && info != NULL && info->saved
			&& info->secured && info->inRange) {
			// The saved password did not work, ask for a new one
			_ShowPrompt(name, info->authentication);
		}
		if (!fPromptBox->IsHidden() && fPromptName == name) {
			BString label(B_TRANSLATE("Enter the password for " QUOTED_NAME
				"."));
			label.ReplaceAll("%name%", name);
			fPromptLabel->SetText(label);
			fPromptError->SetText(error != NULL ? error
				: B_TRANSLATE("Could not connect."));
			fPasswordControl->MakeFocus(true);
			fPasswordControl->TextView()->SelectAll();
		} else {
			_SetMessage(error != NULL ? error
				: B_TRANSLATE("Could not connect."), true);
		}
	}

	_UpdateButtons();
}


const WiFiNetworkInfo*
WiFiWindow::_SelectedNetwork() const
{
	const WiFiNetworkInfo* selected = fOtherList->SelectedNetwork();
	if (selected == NULL)
		selected = fKnownList->SelectedNetwork();
	return selected;
}


void
WiFiWindow::_JoinSelected()
{
	const WiFiNetworkInfo* selected = _SelectedNetwork();
	if (selected == NULL || !selected->inRange
		|| fState.state == WIFI_STATE_OFF
		|| fState.state == WIFI_STATE_NO_ADAPTER) {
		return;
	}
	if (selected->name == fState.current)
		return;

	if (selected->secured && !selected->saved) {
		_ShowPrompt(selected->name, selected->authentication);
		return;
	}

	_HidePrompt();
	BMessage join(kMsgWiFiJoin);
	join.AddString("name", selected->name);
	join.AddBool("remember", true);
	fJoiningName = selected->name;
	_SetMessage("", false);
	fControllerMessenger.SendMessage(&join);
	_UpdateButtons();
}


void
WiFiWindow::_ShowPrompt(const char* name, uint32 authentication)
{
	if (fPromptName != name)
		fPasswordControl->SetText("");

	fPromptName = name;
	fPromptAuthentication = authentication;

	BString label(B_TRANSLATE("Enter the password for " QUOTED_NAME "."));
	label.ReplaceAll("%name%", name);
	fPromptLabel->SetText(label);
	fPromptError->SetText("");

	if (fPromptBox->IsHidden())
		fPromptBox->Show();
	SetDefaultButton(fPromptJoinButton);
	fPasswordControl->MakeFocus(true);
	_UpdateButtons();
}


void
WiFiWindow::_HidePrompt()
{
	fPasswordControl->SetText("");
	fPromptError->SetText("");
	fPromptName = "";
	if (!fPromptBox->IsHidden())
		fPromptBox->Hide();
	SetDefaultButton(fJoinButton);
	_UpdateButtons();
}


void
WiFiWindow::_JoinFromPrompt()
{
	if (fPromptName.IsEmpty() || fJoiningName == fPromptName)
		return;

	const char* password = fPasswordControl->Text();
	if (password[0] == '\0') {
		fPromptError->SetText(B_TRANSLATE("Enter the network password."));
		fPasswordControl->MakeFocus(true);
		return;
	}

	BMessage join(kMsgWiFiJoin);
	join.AddString("name", fPromptName);
	join.AddString("password", password);
	join.AddBool("remember", fRememberCheck->Value() == B_CONTROL_ON);
	if (fPromptAuthentication != B_NETWORK_AUTHENTICATION_NONE)
		join.AddUInt32("authentication", fPromptAuthentication);
	fControllerMessenger.SendMessage(&join);
	join.MakeEmpty();

	fJoiningName = fPromptName;
	BString label(B_TRANSLATE("Connecting to " QUOTED_NAME B_UTF8_ELLIPSIS));
	label.ReplaceAll("%name%", fPromptName);
	fPromptLabel->SetText(label);
	fPromptError->SetText("");
	_SetMessage("", false);
	_UpdateButtons();
}


void
WiFiWindow::_SetMessage(const char* text, bool error)
{
	fMessageView->SetHighUIColor(error ? B_FAILURE_COLOR
		: B_PANEL_TEXT_COLOR);
	fMessageView->SetText(text);
	fMessageView->SetToolTip(text != NULL && text[0] != '\0' ? text : NULL);
}


void
WiFiWindow::_Forget()
{
	const WiFiNetworkInfo* selected = fKnownList->SelectedNetwork();
	if (selected == NULL)
		return;

	BString text(B_TRANSLATE("Forget the Wi-Fi network " QUOTED_NAME "?\n\n"
		"Its saved password is removed and it will no longer be joined "
		"automatically."));
	text.ReplaceAll("%name%", selected->name);
	// Keep the harmless choice as the default button
	BAlert* alert = new BAlert(B_TRANSLATE("Forget network"), text,
		B_TRANSLATE("Forget"), B_TRANSLATE("Cancel"), NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	BMessage* message = new BMessage(kMsgForgetConfirmed);
	message->AddString("name", selected->name);
	alert->Go(new BInvoker(message, this));
}


void
WiFiWindow::_ShowJoinOther()
{
	if (fState.state == WIFI_STATE_NO_ADAPTER) {
		_SetMessage(B_TRANSLATE("There is no Wi-Fi adapter."), true);
		return;
	}

	JoinOtherWindow* window = new JoinOtherWindow(this,
		fControllerMessenger);
	window->Show();
}
