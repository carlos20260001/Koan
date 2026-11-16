/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <Entry.h>
#include <image.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <OS.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <View.h>
#include <Window.h>

#include "WiFiController.h"
#include "WiFiGlyphs.h"
#include "../../shared/net/WirelessNetworkList.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WiFiStatus"

#define QUOTED_NAME B_UTF8_OPEN_QUOTE "%name%" B_UTF8_CLOSE_QUOTE


static const char* kSignature = "application/x-vnd.vos-WiFiStatus";
static const char* kItemName = "WiFiStatus";

static const uint32 kMsgMenuJoin = 'wsjn';
static const uint32 kMsgMenuPower = 'wspw';
static const uint32 kMsgMenuOpen = 'wsop';
static const uint32 kMsgMenuJoinOther = 'wsjo';
static const uint32 kMsgMenuRemove = 'wsqt';
static const uint32 kMsgAnimate = 'wsan';

static const bigtime_t kMenuScanMaxAge = 20000000;


// #pragma mark - WiFiNetworkMenuItem


class WiFiNetworkMenuItem : public BMenuItem {
public:
	WiFiNetworkMenuItem(const WiFiNetworkInfo& info, const char* label,
		BMessage* message)
		:
		BMenuItem(label, message),
		fInfo(info)
	{
	}

	virtual void GetContentSize(float* _width, float* _height)
	{
		BMenuItem::GetContentSize(_width, _height);
		*_width += _GlyphSize() * 3
			+ be_control_look->DefaultLabelSpacing() * 3;
	}

	virtual void DrawContent()
	{
		BMenuItem::DrawContent();

		BMenu* menu = Menu();
		BRect frame = Frame();
		float spacing = be_control_look->DefaultLabelSpacing();
		float size = _GlyphSize();
		float top = floorf(frame.top + (frame.Height() - size) / 2);
		rgb_color foreground = menu->HighColor();
		rgb_color background = menu->LowColor();

		float right = frame.right - spacing;
		BRect bars(right - size * 1.2f, top, right, top + size - 1);
		DrawWiFiSignalBars(menu, bars, fInfo.bars, foreground, background,
			IsEnabled() ? SIGNAL_GLYPH_NORMAL : SIGNAL_GLYPH_DISABLED);

		if (fInfo.secured) {
			right = bars.left - spacing;
			BRect lock(right - size + 1, top, right, top + size - 1);
			DrawWiFiLock(menu, lock,
				WiFiGlyphTint(foreground, background, 0.75f));
		}
	}

private:
	float _GlyphSize() const
	{
		font_height height;
		be_plain_font->GetHeight(&height);
		return floorf((height.ascent + height.descent) * 0.8f);
	}

private:
	WiFiNetworkInfo		fInfo;
};


// #pragma mark - WiFiStatusView


class WiFiStatusView : public BView {
public:
	WiFiStatusView(BRect frame, bool deskbar)
		:
		BView(frame, kItemName, B_FOLLOW_NONE, B_WILL_DRAW),
		fDeskbar(deskbar)
	{
		_Init();
	}

	WiFiStatusView(BMessage* archive)
		:
		BView(archive),
		fDeskbar(true)
	{
		_Init();
	}

	virtual ~WiFiStatusView()
	{
		_StopController();
		delete fAnimationRunner;
	}

	static WiFiStatusView* Instantiate(BMessage* archive);

	virtual status_t Archive(BMessage* archive, bool deep = true) const
	{
		status_t result = BView::Archive(archive, deep);
		if (result == B_OK)
			result = archive->AddString("add_on", kSignature);
		if (result == B_OK)
			result = archive->AddString("class", "WiFiStatusView");
		return result;
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();

		SetViewColor(B_TRANSPARENT_COLOR);
		if (Parent() != NULL) {
			if (Parent()->ViewUIColor() != B_NO_COLOR)
				SetLowUIColor(Parent()->ViewUIColor());
			else
				SetLowColor(Parent()->ViewColor());
		}

		// All network work happens on the controller's thread, never on
		// Deskbar's.
		WiFiController* controller = new WiFiController(BMessenger(this),
			"applet", 0);
		if (controller->Start() == B_OK) {
			fController = BMessenger(controller);
			fControllerThread = controller->Thread();
		} else
			delete controller;
		_UpdateToolTip();
	}

	virtual void DetachedFromWindow()
	{
		_StopController();
		delete fAnimationRunner;
		fAnimationRunner = NULL;
		BView::DetachedFromWindow();
	}

	virtual void Draw(BRect updateRect)
	{
		// The view is transparent: Deskbar draws its own tray background
		// underneath, so only the glyph is drawn here.
		BRect bounds = Bounds();
		BRect glyph = bounds.InsetByCopy(0, 1);

		tray_glyph_state glyphState;
		int32 bars = kWirelessSignalMaxBars;
		switch (fState.state) {
			case WIFI_STATE_NO_ADAPTER:
				glyphState = TRAY_GLYPH_NO_ADAPTER;
				break;
			case WIFI_STATE_OFF:
				glyphState = TRAY_GLYPH_OFF;
				break;
			case WIFI_STATE_DISCONNECTED:
				glyphState = TRAY_GLYPH_DISCONNECTED;
				break;
			case WIFI_STATE_CONNECTING:
			case WIFI_STATE_OBTAINING_ADDRESS:
				glyphState = TRAY_GLYPH_CONNECTING;
				bars = fAnimationPhase + 1;
				break;
			default:
			{
				const WiFiNetworkInfo* current
					= fState.FindNetwork(fState.current);
				if (current != NULL && current->inRange)
					bars = current->bars;
				glyphState = fState.state == WIFI_STATE_CONNECTED
					? TRAY_GLYPH_CONNECTED : TRAY_GLYPH_WEAK;
				break;
			}
		}
		DrawWiFiTrayIcon(this, glyph, bars, glyphState, LowColor());
	}

	virtual void MouseDown(BPoint point)
	{
		// Refresh the list for the next time; this never waits
		BMessage scan(kMsgWiFiScan);
		scan.AddInt64("max_age", kMenuScanMaxAge);
		_SendToController(scan);

		// Like macOS's Option-click, Alt (or Option) shows link details.
		bool details = (modifiers() & (B_COMMAND_KEY | B_OPTION_KEY)) != 0;

		BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);
		menu->SetAsyncAutoDestruct(true);
		menu->SetFont(be_plain_font);
		_BuildMenu(menu, details);
		menu->SetTargetForItems(this);

		ConvertToScreen(&point);
		BRect clickRect = ConvertToScreen(Bounds());
		menu->Go(point, true, true, clickRect, true);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgWiFiStateChanged:
			{
				WiFiState state;
				if (state.Unarchive(*message) != B_OK)
					break;
				bool redraw = _IconChanged(state);
				fState = state;
				_UpdateAnimation();
				_UpdateToolTip();
				if (redraw)
					Invalidate();
				break;
			}

			case kMsgWiFiJoinResult:
				_JoinResult(message);
				break;

			case kMsgAnimate:
				fAnimationPhase = (fAnimationPhase + 1)
					% kWirelessSignalMaxBars;
				Invalidate();
				break;

			case kMsgMenuJoin:
			{
				const char* name;
				if (message->FindString("name", &name) != B_OK)
					break;
				const WiFiNetworkInfo* info = fState.FindNetwork(name);
				if (info != NULL && info->secured && !info->saved) {
					// Let the preferences ask for the password
					_OpenPreferences(name, info->authentication);
					break;
				}
				BMessage join(kMsgWiFiJoin);
				join.AddString("name", name);
				join.AddBool("remember", true);
				_SendToController(join);
				break;
			}

			case kMsgMenuPower:
			{
				BMessage power(kMsgWiFiSetPower);
				power.AddBool("on", fState.state == WIFI_STATE_OFF);
				_SendToController(power);
				break;
			}

			case kMsgMenuOpen:
				be_roster->Launch(kWiFiPreferencesSignature);
				break;

			case kMsgMenuJoinOther:
			{
				BMessage joinOther(kMsgWiFiJoinOther);
				be_roster->Launch(kWiFiPreferencesSignature, &joinOther);
				break;
			}

			case kMsgMenuRemove:
				if (fDeskbar) {
					BDeskbar deskbar;
					deskbar.RemoveItem(kItemName);
				} else
					be_app->PostMessage(B_QUIT_REQUESTED);
				break;

			default:
				BView::MessageReceived(message);
				break;
		}
	}

private:
	void _Init()
	{
		fControllerThread = -1;
		fAnimationRunner = NULL;
		fAnimationPhase = 0;
	}

	void _SendToController(BMessage& message)
	{
		// Never block Deskbar, even if the controller is busy
		fController.SendMessage(&message, (BHandler*)NULL, 100000);
	}

	void _StopController()
	{
		if (fControllerThread < 0)
			return;

		// Deskbar unloads this add-on right after deleting the view, so the
		// controller thread must be gone by then. Its net_server requests
		// time out after a few seconds, which bounds this wait.
		BMessage quit(B_QUIT_REQUESTED);
		fController.SendMessage(&quit, (BHandler*)NULL, 1000000);
		status_t result;
		if (wait_for_thread_etc(fControllerThread, B_RELATIVE_TIMEOUT,
				15000000, &result) != B_OK) {
			// Last resort: a thread running unmapped code would take
			// Deskbar down with it.
			kill_thread(fControllerThread);
			wait_for_thread(fControllerThread, &result);
		}
		fControllerThread = -1;
		fController = BMessenger();
	}

	bool _IconChanged(const WiFiState& state) const
	{
		if (state.state != fState.state)
			return true;
		const WiFiNetworkInfo* before = fState.FindNetwork(fState.current);
		const WiFiNetworkInfo* after = state.FindNetwork(state.current);
		int32 oldBars = before != NULL ? before->bars : -1;
		int32 newBars = after != NULL ? after->bars : -1;
		return oldBars != newBars;
	}

	void _UpdateAnimation()
	{
		bool animate = fState.state == WIFI_STATE_CONNECTING;
		if (animate && fAnimationRunner == NULL) {
			BMessage animation(kMsgAnimate);
			fAnimationRunner = new BMessageRunner(BMessenger(this),
				&animation, 400000);
			fAnimationPhase = 0;
		} else if (!animate && fAnimationRunner != NULL) {
			delete fAnimationRunner;
			fAnimationRunner = NULL;
		}
	}

	void _UpdateToolTip()
	{
		BString text;
		const WiFiNetworkInfo* current = fState.FindNetwork(fState.current);
		switch (fState.state) {
			case WIFI_STATE_NO_ADAPTER:
				text = B_TRANSLATE("Wi-Fi: No adapter");
				break;
			case WIFI_STATE_OFF:
				text = B_TRANSLATE("Wi-Fi: Off");
				break;
			case WIFI_STATE_DISCONNECTED:
				text = B_TRANSLATE("Wi-Fi: Not connected");
				break;
			case WIFI_STATE_CONNECTING:
				text = B_TRANSLATE("Wi-Fi: Connecting to " QUOTED_NAME
					B_UTF8_ELLIPSIS);
				break;
			case WIFI_STATE_OBTAINING_ADDRESS:
				text = B_TRANSLATE("Wi-Fi: " QUOTED_NAME
					", obtaining an IP address" B_UTF8_ELLIPSIS);
				break;
			case WIFI_STATE_NO_INTERNET:
				text = B_TRANSLATE("Wi-Fi: " QUOTED_NAME ", no Internet");
				break;
			case WIFI_STATE_CONNECTED:
				text = B_TRANSLATE("Wi-Fi: Connected to " QUOTED_NAME);
				break;
		}
		text.ReplaceAll("%name%", fState.current);
		if (fState.IsAssociated() && current != NULL && current->inRange) {
			BString signal(B_TRANSLATE("Signal: %percent%% (%dBm% dBm)"));
			BString value;
			value << current->percent;
			signal.ReplaceFirst("%percent%", value);
			value = "";
			value << current->dBm;
			signal.ReplaceFirst("%dBm%", value);
			text << "\n" << signal;
		}
		if (!fState.address.IsEmpty() && fState.IsAssociated())
			text << "\n" << fState.address;

		if (text != fToolTipText) {
			fToolTipText = text;
			SetToolTip(text);
		}
	}

	void _AddNetworkItem(BMenu* menu, const WiFiNetworkInfo& info,
		bool current)
	{
		BString label(info.name);
		if (current && fState.state == WIFI_STATE_CONNECTING)
			label << " " << B_TRANSLATE("(connecting" B_UTF8_ELLIPSIS ")");
		else if (current && fState.state == WIFI_STATE_NO_INTERNET)
			label << " " << B_TRANSLATE("(no Internet)");

		BMessage* message = NULL;
		if (!current) {
			message = new BMessage(kMsgMenuJoin);
			message->AddString("name", info.name);
		}
		WiFiNetworkMenuItem* item = new WiFiNetworkMenuItem(info, label,
			message);
		item->SetMarked(current);
		menu->AddItem(item);
	}

	void _AddDetail(BMenu* menu, const char* label, const BString& value)
	{
		if (value.IsEmpty())
			return;
		BString text("    ");
		text << label << ": " << value;
		BMenuItem* item = new BMenuItem(text, NULL);
		item->SetEnabled(false);
		menu->AddItem(item);
	}

	void _AddLinkDetails(BMenu* menu)
	{
		const WiFiLinkDetails& link = fState.link;
		_AddDetail(menu, B_TRANSLATE("IP address"), link.address);
		_AddDetail(menu, B_TRANSLATE("Router"), link.router);
		_AddDetail(menu, B_TRANSLATE("Security"), link.security);
		_AddDetail(menu, B_TRANSLATE("BSSID"), link.bssid);
		if (link.channel > 0) {
			BString channel;
			channel << link.channel << " (";
			if (link.frequency >= 5925)
				channel << "6 GHz";
			else if (link.frequency >= 4900)
				channel << "5 GHz";
			else
				channel << "2.4 GHz";
			channel << ")";
			_AddDetail(menu, B_TRANSLATE("Channel"), channel);
		}
		if (link.dBm != 0) {
			BString rssi;
			rssi << link.dBm << " dBm";
			_AddDetail(menu, B_TRANSLATE("RSSI"), rssi);
		}
		if (link.txKbps > 0) {
			BString rate;
			if (link.txKbps % 1000 == 0 || link.txKbps >= 100000)
				rate << (link.txKbps + 500) / 1000;
			else
				rate.SetToFormat("%.1f", link.txKbps / 1000.0);
			rate << " Mbps";
			if (!link.txDetails.IsEmpty())
				rate << " (" << link.txDetails << ")";
			_AddDetail(menu, B_TRANSLATE("Tx rate"), rate);
		}
		_AddDetail(menu, B_TRANSLATE("PHY mode"), link.phyMode);
		// Country code and noise are not reported by the wireless drivers
		// yet.
	}

	void _BuildMenu(BPopUpMenu* menu, bool details)
	{
		bool available = fState.state != WIFI_STATE_NO_ADAPTER;
		bool on = available && fState.state != WIFI_STATE_OFF;

		BMenuItem* item;
		if (!available) {
			item = new BMenuItem(B_TRANSLATE("Wi-Fi: No adapter found"),
				NULL);
			item->SetEnabled(false);
			menu->AddItem(item);
		} else {
			item = new BMenuItem(on ? B_TRANSLATE("Wi-Fi: On")
				: B_TRANSLATE("Wi-Fi: Off"), NULL);
			item->SetEnabled(false);
			menu->AddItem(item);
			menu->AddItem(new BMenuItem(on
				? B_TRANSLATE("Turn Wi-Fi off")
				: B_TRANSLATE("Turn Wi-Fi on"),
				new BMessage(kMsgMenuPower)));
		}

		if (on) {
			menu->AddSeparatorItem();

			const WiFiNetworkInfo* current
				= fState.FindNetwork(fState.current);
			if (!fState.current.IsEmpty()) {
				WiFiNetworkInfo info;
				if (current != NULL)
					info = *current;
				else {
					info.name = fState.current;
					info.inRange = true;
				}
				_AddNetworkItem(menu, info, true);
				if (details && fState.IsAssociated())
					_AddLinkDetails(menu);
				menu->AddSeparatorItem();
			}

			int32 known = 0;
			int32 other = 0;
			for (int pass = 0; pass < 2; pass++) {
				bool savedPass = pass == 0;
				bool headerAdded = false;
				for (size_t i = 0; i < fState.networks.size(); i++) {
					const WiFiNetworkInfo& info = fState.networks[i];
					if (!info.inRange || info.saved != savedPass
						|| info.name == fState.current) {
						continue;
					}
					if (!headerAdded) {
						item = new BMenuItem(savedPass
							? B_TRANSLATE("Known networks")
							: B_TRANSLATE("Other networks"), NULL);
						item->SetEnabled(false);
						menu->AddItem(item);
						headerAdded = true;
					}
					_AddNetworkItem(menu, info, false);
					if (savedPass)
						known++;
					else
						other++;
				}
			}

			if (known == 0 && other == 0) {
				item = new BMenuItem(fState.scanning
					? B_TRANSLATE("Scanning" B_UTF8_ELLIPSIS)
					: B_TRANSLATE("No other networks found"), NULL);
				item->SetEnabled(false);
				menu->AddItem(item);
			}
		}

		menu->AddSeparatorItem();
		if (on) {
			menu->AddItem(new BMenuItem(
				B_TRANSLATE("Join other network" B_UTF8_ELLIPSIS),
				new BMessage(kMsgMenuJoinOther)));
		}
		menu->AddItem(new BMenuItem(
			B_TRANSLATE("Open Wi-Fi preferences" B_UTF8_ELLIPSIS),
			new BMessage(kMsgMenuOpen)));
		if (fDeskbar) {
			menu->AddItem(new BMenuItem(B_TRANSLATE("Remove from Deskbar"),
				new BMessage(kMsgMenuRemove)));
		}
	}

	void _OpenPreferences(const char* name, uint32 authentication,
		const char* error = NULL)
	{
		BMessage select(kMsgWiFiSelectNetwork);
		select.AddString("name", name);
		if (error != NULL)
			select.AddString("error", error);
		select.AddString("device", fState.device);
		select.AddUInt32("authentication", authentication);
		status_t status = be_roster->Launch(kWiFiPreferencesSignature,
			&select);
		if (status == B_ALREADY_RUNNING) {
			BMessenger preferences(kWiFiPreferencesSignature);
			preferences.SendMessage(&select, (BHandler*)NULL, 500000);
		}
	}

	void _JoinResult(BMessage* message)
	{
		const char* name = message->GetString("name", "");
		status_t status = message->GetInt32("status", B_ERROR);
		if (status == B_OK || status == B_CANCELED)
			return;

		const char* error = message->GetString("error", NULL);
		const WiFiNetworkInfo* info = fState.FindNetwork(name);
		if (status == B_NOT_ALLOWED || (info != NULL && info->secured)) {
			// A password is missing or was wrong: ask for it in the
			// preferences, which also show the error.
			_OpenPreferences(name, info != NULL ? info->authentication
				: B_NETWORK_AUTHENTICATION_WPA2,
				status == B_NOT_ALLOWED ? NULL : error);
			return;
		}

		BString text;
		if (error != NULL)
			text = error;
		else {
			text = B_TRANSLATE("Could not connect to " QUOTED_NAME ".");
			text.ReplaceAll("%name%", name);
		}
		BAlert* alert = new BAlert(B_TRANSLATE("Wi-Fi"), text,
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
	}

private:
	bool				fDeskbar;
	BMessenger			fController;
	thread_id			fControllerThread;
	WiFiState			fState;
	BMessageRunner*		fAnimationRunner;
	int32				fAnimationPhase;
	BString				fToolTipText;
};


WiFiStatusView*
WiFiStatusView::Instantiate(BMessage* archive)
{
	return validate_instantiation(archive, "WiFiStatusView")
		? new WiFiStatusView(archive) : NULL;
}


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	return new WiFiStatusView(BRect(0, 0, maxHeight - 1, maxHeight - 1),
		true);
}


// #pragma mark - WiFiStatusApp


class WiFiStatusApp : public BApplication {
public:
	WiFiStatusApp()
		:
		BApplication(kSignature)
	{
	}

	virtual void ReadyToRun()
	{
		// Installs the replicant into Deskbar, which then runs it
		BDeskbar deskbar;
		for (int32 attempt = 0; attempt < 60 && !deskbar.IsRunning();
				attempt++) {
			snooze(1000000);
		}
		if (deskbar.IsRunning() && !deskbar.HasItem(kItemName)) {
			image_info info;
			int32 cookie = 0;
			while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info)
					== B_OK) {
				if ((char*)instantiate_deskbar_item < (char*)info.text
					|| (char*)instantiate_deskbar_item
						>= (char*)info.text + info.text_size) {
					continue;
				}
				entry_ref ref;
				status_t result = get_ref_for_path(info.name, &ref);
				if (result == B_OK)
					result = deskbar.AddItem(&ref);
				if (result != B_OK) {
					fprintf(stderr, "WiFiStatus: Deskbar install failed: "
						"%s\n", strerror(result));
				}
				break;
			}
		}
		Quit();
	}
};


int
main()
{
	WiFiStatusApp app;
	app.Run();
	return 0;
}
