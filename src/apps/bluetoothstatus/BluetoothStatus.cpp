/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Bluetooth Deskbar applet: shows whether Bluetooth is available, which
 * paired devices are connected, and their battery level where the device
 * reports one.
 */


#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <Application.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <GradientLinear.h>
#include <IconUtils.h>
#include <image.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Roster.h>
#include <View.h>
#include <Window.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>

#include <AudioSinkSetting.h>
#include <LaunchRoster.h>
#include <LEBondStore.h>
#include <LEDeviceStatus.h>
#include <bluetoothserver_p.h>

#include <algorithm>
#include <map>
#include <vector>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BluetoothStatus"


extern char** environ;

static const char* kSignature = "application/x-vnd.vos-BluetoothStatus";
static const char* kItemName = "BluetoothStatus";
static const char* kPreferencesSignature
	= "application/x-vnd.vos-BluetoothPrefs";
	// The replicant bluetooth_server used to install; an older system may
	// have left it in the Deskbar.
static const char* kServerItemName = "BluetoothServerReplicant";
static const char* kServerJob = "x-vnd.vos-bluetooth_server";

static const uint32 kMsgRefresh = 'btrf';
static const uint32 kMsgOpenPreferences = 'btop';
static const uint32 kMsgStartService = 'btss';
static const uint32 kMsgRemove = 'btrm';
static const uint32 kMsgAudioOutput = 'btao';

static const bigtime_t kRefreshInterval = 3000000;


enum device_kind {
	DEVICE_KIND_GENERIC = 0,
	DEVICE_KIND_MOUSE,
	DEVICE_KIND_KEYBOARD,
	DEVICE_KIND_INPUT,
	DEVICE_KIND_AUDIO,
	DEVICE_KIND_PHONE,
	DEVICE_KIND_COMPUTER,
	DEVICE_KIND_GAMEPAD
};


struct StatusDevice {
	uint8		address[6];
	BString		name;
	int32		kind;
	bool		lowEnergy;
	bool		connected;
	bool		stateKnown;
		// Whether "connected" is known (the Bluetooth service may not say)
	bool		audioOutput;
		// sound plays through it
	bool		playsAudio;
	int32		battery;
};


static BString
AddressText(const uint8 address[6])
{
	char text[18];
	snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
		address[5], address[4], address[3], address[2], address[1],
		address[0]);
	return text;
}


static int32
KindForClass(const uint8 deviceClass[3])
{
	// Class of Device: minor bits 7-2 of byte 0, major class in byte 1.
	uint8 major = deviceClass[1] & 0x1f;
	uint8 minor = deviceClass[0] >> 2;
	switch (major) {
		case 0x01:
			return DEVICE_KIND_COMPUTER;
		case 0x02:
			return DEVICE_KIND_PHONE;
		case 0x04:
			return DEVICE_KIND_AUDIO;
		case 0x05:
			if ((minor & 0x20) != 0 && (minor & 0x10) == 0)
				return DEVICE_KIND_KEYBOARD;
			if ((minor & 0x10) != 0 && (minor & 0x20) == 0)
				return DEVICE_KIND_MOUSE;
			if ((minor & 0x0f) == 0x01 || (minor & 0x0f) == 0x02)
				return DEVICE_KIND_GAMEPAD;
			return DEVICE_KIND_INPUT;
		default:
			return DEVICE_KIND_GENERIC;
	}
}


static const char*
IconNameForKind(int32 kind)
{
	switch (kind) {
		case DEVICE_KIND_MOUSE:
			return "device:mouse";
		case DEVICE_KIND_KEYBOARD:
		case DEVICE_KIND_INPUT:
			return "device:keyboard";
		case DEVICE_KIND_AUDIO:
			return "device:audio";
		case DEVICE_KIND_COMPUTER:
			return "device:computer";
		case DEVICE_KIND_GAMEPAD:
			return "device:gamepad";
		default:
			return "BEOS:ICON";
	}
}


//!	Opens the resources of the image this code lives in (Deskbar loads the
//	applet as an add-on, so be_app's resources are Deskbar's).
static BResources*
OwnResources()
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if ((char*)OwnResources < (char*)info.text
			|| (char*)OwnResources >= (char*)info.text + info.text_size)
			continue;
		BFile file(info.name, B_READ_ONLY);
		if (file.InitCheck() != B_OK)
			return NULL;
		BResources* resources = new BResources();
		if (resources->SetTo(&file) != B_OK) {
			delete resources;
			return NULL;
		}
		return resources;
	}
	return NULL;
}


class IconCache {
public:
	IconCache()
		:
		fResources(OwnResources())
	{
	}

	~IconCache()
	{
		for (auto& entry : fIcons)
			delete entry.second;
		delete fResources;
	}

	const BBitmap* Get(const char* name, float size, int32 type
		= B_VECTOR_ICON_TYPE)
	{
		int32 pixels = (int32)size;
		BString key(name);
		key << "/" << pixels;
		auto found = fIcons.find(key);
		if (found != fIcons.end())
			return found->second;
		BBitmap* icon = NULL;
		size_t length;
		const void* data = fResources != NULL
			? fResources->LoadResource(type, name, &length) : NULL;
		if (data != NULL) {
			icon = new BBitmap(BRect(0, 0, pixels - 1, pixels - 1),
				B_RGBA32);
			if (icon->InitCheck() != B_OK
				|| BIconUtils::GetVectorIcon((const uint8*)data, length,
					icon) != B_OK) {
				delete icon;
				icon = NULL;
			}
		}
		fIcons[key] = icon;
		return icon;
	}

private:
	BResources*		fResources;
	std::map<BString, BBitmap*> fIcons;
};


/*!	Draws a small battery in the style of the Wi-Fi bars: dark outline,
	gradient fill in green, amber or red by level.
*/
static void
DrawBattery(BView* view, BRect frame, int32 percent)
{
	float height = std::max(6.0f, floorf(frame.Height() * 0.55f));
	float width = floorf(height * 1.9f);
	BRect body(frame.left, floorf(frame.top + (frame.Height() - height) / 2),
		frame.left + width - 2, 0);
	body.bottom = body.top + height;
	BRect tip(body.right + 1, body.top + floorf(height / 3), body.right + 2,
		body.bottom - floorf(height / 3));

	rgb_color face, highlight, outline;
	if (percent > 30) {
		face = make_color(70, 190, 50);
		highlight = make_color(170, 240, 120);
		outline = make_color(20, 70, 15);
	} else if (percent > 15) {
		face = make_color(240, 160, 20);
		highlight = make_color(255, 225, 110);
		outline = make_color(110, 60, 0);
	} else {
		face = make_color(215, 40, 30);
		highlight = make_color(255, 140, 120);
		outline = make_color(100, 10, 5);
	}

	view->PushState();
	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(make_color(40, 40, 40));
	view->StrokeRect(body);
	view->FillRect(tip);
	BRect inside = body.InsetByCopy(1, 1);
	view->SetHighColor(ui_color(B_MENU_BACKGROUND_COLOR));
	view->FillRect(inside);
	BRect level = inside;
	level.right = floorf(level.left + (inside.Width() + 1) * percent / 100) - 1;
	if (level.IsValid()) {
		BGradientLinear gradient(level.LeftTop(), level.LeftBottom());
		gradient.AddColor(highlight, 0);
		gradient.AddColor(face, 255);
		view->FillRect(level, gradient);
		view->SetHighColor(outline);
		view->StrokeLine(level.RightTop(), level.RightBottom());
	}
	view->PopState();
}


class DeviceMenuItem : public BMenuItem {
public:
	DeviceMenuItem(const StatusDevice& device, const BBitmap* icon,
		BMessage* message)
		:
		BMenuItem(device.name.String(), message),
		fDevice(device),
		fIcon(icon)
	{
	}

	virtual void GetContentSize(float* _width, float* _height)
	{
		BMenuItem::GetContentSize(_width, _height);
		font_height fontHeight;
		Menu()->GetFontHeight(&fontHeight);
		float lineHeight = ceilf(fontHeight.ascent + fontHeight.descent);
		if (_height != NULL)
			*_height = std::max(*_height, lineHeight + 2);
		if (_width != NULL) {
			*_width += lineHeight + 8
				+ Menu()->StringWidth(_StatusText().String()) + 16;
			if (_ShowsBattery())
				*_width += lineHeight * 1.2f + 4;
		}
	}

	virtual void DrawContent()
	{
		BMenu* menu = Menu();
		BRect frame = Frame();
		font_height fontHeight;
		menu->GetFontHeight(&fontHeight);
		float lineHeight = ceilf(fontHeight.ascent + fontHeight.descent);

		BRect iconRect(frame.left + 8, frame.top + 1, 0, 0);
		iconRect.right = iconRect.left + lineHeight - 1;
		iconRect.bottom = iconRect.top + lineHeight - 1;
		iconRect.OffsetBy(0, floorf((frame.Height() - lineHeight) / 2));
		if (fIcon != NULL) {
			menu->PushState();
			menu->SetDrawingMode(B_OP_ALPHA);
			menu->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			menu->DrawBitmap(fIcon, fIcon->Bounds(), iconRect,
				B_FILTER_BITMAP_BILINEAR);
			menu->PopState();
		}

		// Name
		BPoint location = ContentLocation();
		location.x = iconRect.right + 6;
		location.y = frame.top + floorf((frame.Height() + fontHeight.ascent
			- fontHeight.descent) / 2);
		rgb_color text = ui_color(IsSelected() ? B_MENU_SELECTED_ITEM_TEXT_COLOR
			: B_MENU_ITEM_TEXT_COLOR);
		menu->SetHighColor(text);
		menu->SetLowColor(ui_color(IsSelected()
			? B_MENU_SELECTED_BACKGROUND_COLOR : B_MENU_BACKGROUND_COLOR));
		if (fDevice.connected) {
			BFont bold;
			menu->GetFont(&bold);
			bold.SetFace(B_BOLD_FACE);
			menu->SetFont(&bold, B_FONT_FACE);
		}
		menu->DrawString(fDevice.name.String(), location);
		if (fDevice.connected) {
			BFont regular;
			menu->GetFont(&regular);
			regular.SetFace(B_REGULAR_FACE);
			menu->SetFont(&regular, B_FONT_FACE);
		}

		// Status and battery, right aligned
		float right = frame.right - 18;
		if (_ShowsBattery()) {
			BString percent;
			percent << fDevice.battery << "%";
			float width = menu->StringWidth(percent.String());
			menu->SetHighColor(text);
			menu->DrawString(percent.String(), BPoint(right - width,
				location.y));
			right -= width + 4;
			BRect battery(right - lineHeight * 1.2f, frame.top, right,
				frame.bottom);
			DrawBattery(menu, battery, fDevice.battery);
		} else {
			BString status = _StatusText();
			float width = menu->StringWidth(status.String());
			menu->SetHighColor(tint_color(text, text.IsDark()
				? B_LIGHTEN_1_TINT : B_DARKEN_1_TINT));
			menu->DrawString(status.String(), BPoint(right - width,
				location.y));
		}
	}

private:
	bool _ShowsBattery() const
	{
		return fDevice.connected && fDevice.battery >= 0;
	}

	BString _StatusText() const
	{
		if (fDevice.audioOutput)
			return fDevice.connected ? B_TRANSLATE("Sound output")
				: B_TRANSLATE("Sound output, idle");
		if (!fDevice.stateKnown)
			return B_TRANSLATE("Paired");
		return fDevice.connected ? B_TRANSLATE("Connected")
			: B_TRANSLATE("Not connected");
	}

	StatusDevice	fDevice;
	const BBitmap*	fIcon;
};


// #pragma mark - BluetoothStatusView


class BluetoothStatusView : public BView {
public:
	BluetoothStatusView(BRect frame, bool deskbar)
		:
		BView(frame, kItemName, B_FOLLOW_NONE, B_WILL_DRAW),
		fDeskbar(deskbar)
	{
		_Init();
	}

	BluetoothStatusView(BMessage* archive)
		:
		BView(archive),
		fDeskbar(true)
	{
		_Init();
	}

	virtual ~BluetoothStatusView()
	{
		delete fRefreshRunner;
		delete fIcons;
	}

	static BluetoothStatusView* Instantiate(BMessage* archive);

	virtual status_t Archive(BMessage* archive, bool deep = true) const
	{
		status_t result = BView::Archive(archive, deep);
		if (result == B_OK)
			result = archive->AddString("add_on", kSignature);
		if (result == B_OK)
			result = archive->AddString("class", "BluetoothStatusView");
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
		fIcons = new IconCache();
		BMessage refresh(kMsgRefresh);
		fRefreshRunner = new BMessageRunner(BMessenger(this), &refresh,
			kRefreshInterval);
		_Refresh();
	}

	virtual void DetachedFromWindow()
	{
		delete fRefreshRunner;
		fRefreshRunner = NULL;
		BView::DetachedFromWindow();
	}

	virtual void Draw(BRect updateRect)
	{
		// The view is transparent: Deskbar draws its tray background.
		BRect bounds = Bounds();

		float size = std::min(bounds.Width(), bounds.Height()) + 1;
		const BBitmap* icon = fIcons != NULL
			? fIcons->Get("tray_icon", size) : NULL;
		if (icon == NULL)
			return;

		SetDrawingMode(B_OP_ALPHA);
		if (fServerRunning) {
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			DrawBitmap(icon, BPoint(0, 0));
		} else {
			// Faded while the Bluetooth service is not running.
			SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
			SetHighColor(0, 0, 0, 90);
			DrawBitmap(icon, BPoint(0, 0));
		}

		if (fConnectedCount > 0) {
			// A BeOS-style status light: outlined, lit from the top left.
			float light = std::max(5.0f, floorf(size * 0.4f));
			BRect dot(bounds.right - light + 1, bounds.bottom - light + 1,
				bounds.right, bounds.bottom);
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			BGradientLinear gradient(dot.LeftTop(), dot.RightBottom());
			gradient.AddColor(make_color(190, 255, 150), 0);
			gradient.AddColor(make_color(40, 160, 30), 255);
			FillEllipse(dot, gradient);
			SetHighColor(15, 70, 10);
			StrokeEllipse(dot);
		}
		SetDrawingMode(B_OP_COPY);
	}

	virtual void MouseDown(BPoint point)
	{
		_Refresh();
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
			case kMsgRefresh:
				_Refresh();
				break;

			case kMsgOpenPreferences:
				be_roster->Launch(kPreferencesSignature);
				break;

			case kMsgStartService:
				_StartService();
				break;

			case kMsgRemove:
				BDeskbar().RemoveItem(kItemName);
				break;

			case kMsgAudioOutput:
			{
				// The Bluetooth audio output follows this setting: it
				// connects to the device and takes over the sound, or gives
				// it back.
				const void* data;
				ssize_t size;
				if (message->GetBool("use", false)
					&& message->FindData("address", B_RAW_TYPE, &data, &size)
						== B_OK && size == 6) {
					bdaddr_t address;
					memcpy(address.b, data, 6);
					Bluetooth::SetAudioSink(&address,
						message->GetString("name", NULL));
				} else
					Bluetooth::SetAudioSink(NULL, NULL);
				_Refresh();
				break;
			}

			default:
				BView::MessageReceived(message);
		}
	}

private:
	void _Init()
	{
		fIcons = NULL;
		fRefreshRunner = NULL;
		fServerRunning = false;
		fConnectedCount = 0;
	}

	void _Refresh()
	{
		bool running = be_roster->IsRunning(BLUETOOTH_SIGNATURE);
		std::vector<StatusDevice> devices;
		_LoadDevices(devices);
		int32 connected = 0;
		for (size_t i = 0; i < devices.size(); i++) {
			if (devices[i].connected)
				connected++;
		}
		bool changed = running != fServerRunning
			|| connected != fConnectedCount;
		fServerRunning = running;
		fConnectedCount = connected;
		fDevices = devices;
		if (changed)
			Invalidate();
		_UpdateToolTip();
	}

	void _LoadNames(std::map<BString, std::pair<BString, int32> >& names)
	{
		BPath path;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
			|| path.Append("Bluetooth_device_names") != B_OK)
			return;
		BFile file(path.Path(), B_READ_ONLY);
		BMessage archive;
		if (file.InitCheck() != B_OK || archive.Unflatten(&file) != B_OK)
			return;
		BMessage device;
		for (int32 i = 0; archive.FindMessage("device", i, &device) == B_OK;
				i++) {
			const void* address;
			ssize_t size;
			const char* name;
			if (device.FindData("address", B_RAW_TYPE, &address, &size)
					!= B_OK || size != 6
				|| device.FindString("name", &name) != B_OK)
				continue;
			names[AddressText((const uint8*)address)] = std::make_pair(
				BString(name), device.GetInt32("kind", DEVICE_KIND_GENERIC));
		}
	}

	void _LoadDevices(std::vector<StatusDevice>& devices)
	{
		std::map<BString, std::pair<BString, int32> > names;
		_LoadNames(names);

		// Low Energy bonds, with live state from the device's link owner.
		char directory[B_PATH_NAME_LENGTH];
		std::vector<Bluetooth::LEBondedDevice> bonds;
		if (Bluetooth::DefaultLEBondDirectory(directory, sizeof(directory))
				== B_OK)
			Bluetooth::ListLEBonds(directory, bonds);
		std::vector<Bluetooth::LEDeviceStatus> status;
		Bluetooth::ReadLEDeviceStatus(status);
		for (size_t i = 0; i < bonds.size(); i++) {
			StatusDevice device;
			memcpy(device.address, bonds[i].peerAddress, 6);
			device.lowEnergy = true;
			device.kind = bonds[i].mouse ? DEVICE_KIND_MOUSE
				: DEVICE_KIND_GENERIC;
			device.connected = false;
			device.stateKnown = true;
			device.audioOutput = false;
			device.playsAudio = false;
			device.battery = -1;
			BString key = AddressText(device.address);
			auto name = names.find(key);
			if (name != names.end()) {
				device.name = name->second.first;
				if (name->second.second != DEVICE_KIND_GENERIC)
					device.kind = name->second.second;
			}
			if (device.name.IsEmpty())
				device.name = key;
			for (size_t j = 0; j < status.size(); j++) {
				if (memcmp(status[j].address, device.address, 6) != 0)
					continue;
				device.connected = status[j].connected;
				device.battery = status[j].battery;
			}
			devices.push_back(device);
		}

		// Classic pairings, as the Bluetooth service stores them, with their
		// link state if it answers quickly.
		bdaddr_t audioSink;
		BString audioSinkName;
		bool hasAudioSink = Bluetooth::GetAudioSink(audioSink, audioSinkName)
			== B_OK;
		BMessenger server(BLUETOOTH_SIGNATURE);
		hci_id hid = -1;
		{
			BMessage request(BT_MSG_ACQUIRE_LOCAL_DEVICE);
			BMessage reply;
			if (!server.IsValid()
				|| server.SendMessage(&request, &reply, 200000, 200000) != B_OK
				|| reply.FindInt32("hci_id", &hid) != B_OK)
				hid = -1;
		}
		BPath path;
		BMessage archive;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK
			&& path.Append("Bluetooth_paired_devices") == B_OK) {
			BFile file(path.Path(), B_READ_ONLY);
			if (file.InitCheck() == B_OK)
				archive.Unflatten(&file);
		}
		BMessage remote;
		for (int32 i = 0; archive.FindMessage("remote", i, &remote) == B_OK;
				i++) {
			const void* address;
			ssize_t size;
			if (remote.FindData("bdaddr", B_ANY_TYPE, &address, &size)
					!= B_OK || size != 6)
				continue;
			StatusDevice device;
			memcpy(device.address, address, 6);
			device.lowEnergy = false;
			device.connected = false;
			device.stateKnown = false;
			device.audioOutput = hasAudioSink
				&& memcmp(audioSink.b, address, 6) == 0;
			device.battery = -1;
			if (hid >= 0) {
				BMessage request(BT_REQ_CONN_STATE);
				request.AddInt32("hci_id", hid);
				request.AddData("bdaddr", B_ANY_TYPE, address, 6);
				BMessage reply;
				uint8 state;
				if (server.SendMessage(&request, &reply, 200000, 200000)
						== B_OK
					&& reply.FindUInt8("conn state", &state) == B_OK) {
					device.stateKnown = true;
					device.connected = state == 0;
						// RemoteDevice::CONNECTED
				}
			}
			uint8 deviceClass[3] = {};
			const void* classData;
			if (remote.FindData("class_of_device", B_RAW_TYPE, &classData,
					&size) == B_OK && size == 3)
				memcpy(deviceClass, classData, 3);
			device.kind = KindForClass(deviceClass);
			// Rendering and Audio service bits (Class of Device bits 18 and
			// 21): it can play sound, whatever its major class.
			device.playsAudio = device.kind == DEVICE_KIND_AUDIO
				|| (deviceClass[2] & 0x24) == 0x24;
			BString key = AddressText(device.address);
			auto name = names.find(key);
			const char* storedName;
			if (name != names.end())
				device.name = name->second.first;
			else if (remote.FindString("name", &storedName) == B_OK
				&& storedName[0] != '\0' && storedName[0] != '#')
				device.name = storedName;
			if (device.name.IsEmpty())
				device.name = key;
			bool duplicate = false;
			for (size_t j = 0; j < devices.size(); j++) {
				if (memcmp(devices[j].address, device.address, 6) == 0)
					duplicate = true;
			}
			if (!duplicate)
				devices.push_back(device);
		}

		std::stable_sort(devices.begin(), devices.end(),
			[](const StatusDevice& a, const StatusDevice& b) {
				if (a.connected != b.connected)
					return a.connected;
				// Devices known by name before bare addresses.
				bool aNamed = a.name != AddressText(a.address);
				bool bNamed = b.name != AddressText(b.address);
				if (aNamed != bNamed)
					return aNamed;
				return a.name.ICompare(b.name) < 0;
			});
	}

	void _UpdateToolTip()
	{
		BString text;
		if (!fServerRunning)
			text = B_TRANSLATE("Bluetooth: Service not running");
		else if (fConnectedCount == 0)
			text = B_TRANSLATE("Bluetooth: No devices connected");
		else {
			text = B_TRANSLATE("Bluetooth:");
			for (size_t i = 0; i < fDevices.size(); i++) {
				if (!fDevices[i].connected)
					continue;
				text << "\n" << fDevices[i].name;
				if (fDevices[i].battery >= 0)
					text << " (" << fDevices[i].battery << "%)";
			}
		}
		if (text != fToolTipText) {
			fToolTipText = text;
			SetToolTip(text.String());
		}
	}

	void _AddDetail(BMenu* menu, const char* label, const BString& value)
	{
		BString text("        ");
		text << label << ": " << value;
		BMenuItem* item = new BMenuItem(text, NULL);
		item->SetEnabled(false);
		menu->AddItem(item);
	}

	void _BuildMenu(BPopUpMenu* menu, bool details)
	{
		BMenuItem* item = new BMenuItem(fServerRunning
			? B_TRANSLATE("Bluetooth: On")
			: B_TRANSLATE("Bluetooth: Service not running"), NULL);
		item->SetEnabled(false);
		menu->AddItem(item);
		if (!fServerRunning) {
			menu->AddItem(new BMenuItem(
				B_TRANSLATE("Start Bluetooth service"),
				new BMessage(kMsgStartService)));
		}
		menu->AddSeparatorItem();

		item = new BMenuItem(B_TRANSLATE("Devices"), NULL);
		item->SetEnabled(false);
		menu->AddItem(item);
		if (fDevices.empty()) {
			item = new BMenuItem(B_TRANSLATE("No paired devices"), NULL);
			item->SetEnabled(false);
			menu->AddItem(item);
		}
		font_height fontHeight;
		be_plain_font->GetHeight(&fontHeight);
		float iconSize = ceilf(fontHeight.ascent + fontHeight.descent);
		for (size_t i = 0; i < fDevices.size(); i++) {
			const StatusDevice& device = fDevices[i];
			menu->AddItem(new DeviceMenuItem(device,
				fIcons->Get(IconNameForKind(device.kind), iconSize),
				new BMessage(kMsgOpenPreferences)));
			if (!device.lowEnergy && (device.playsAudio
					|| device.audioOutput)) {
				BMessage* choice = new BMessage(kMsgAudioOutput);
				choice->AddBool("use", !device.audioOutput);
				choice->AddData("address", B_RAW_TYPE, device.address, 6);
				choice->AddString("name", device.name);
				BMenuItem* audio = new BMenuItem(device.audioOutput
					? B_TRANSLATE("        Stop playing sound here")
					: B_TRANSLATE("        Play sound here"), choice);
				menu->AddItem(audio);
			}
			if (details) {
				_AddDetail(menu, B_TRANSLATE("Address"),
					AddressText(device.address));
				_AddDetail(menu, B_TRANSLATE("Type"), device.lowEnergy
					? B_TRANSLATE("Bluetooth Low Energy")
					: B_TRANSLATE("Bluetooth Classic"));
				BString battery;
				if (device.battery >= 0)
					battery << device.battery << "%";
				else if (device.lowEnergy && device.connected)
					battery = B_TRANSLATE("Not reported by the device");
				else
					battery = B_TRANSLATE("Unknown");
				_AddDetail(menu, B_TRANSLATE("Battery"), battery);
			}
		}

		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem(
			B_TRANSLATE("Open Bluetooth preferences" B_UTF8_ELLIPSIS),
			new BMessage(kMsgOpenPreferences)));
		if (fDeskbar) {
			menu->AddItem(new BMenuItem(B_TRANSLATE("Remove from Deskbar"),
				new BMessage(kMsgRemove)));
		}
	}

	void _StartService()
	{
		// The launch_daemon runs the server as a service, and starts it
		// again after the preferences stopped it.
		if (BLaunchRoster().Start(kServerJob) == B_OK)
			return;

		// Without that job, a system can supply a hook that prepares its
		// controller and starts the server; run it as its own process so
		// Deskbar never waits for it.
		BPath hook;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &hook) == B_OK
			&& hook.Append("bluetooth/start-services") == B_OK
			&& access(hook.Path(), R_OK) == 0) {
			char* const arguments[] = { (char*)"sh", (char*)hook.Path(),
				NULL };
			pid_t process;
			if (posix_spawn(&process, "/bin/sh", NULL, NULL, arguments,
					environ) == 0)
				return;
		}
		be_roster->Launch(BLUETOOTH_SIGNATURE);
	}

	bool				fDeskbar;
	IconCache*			fIcons;
	BMessageRunner*		fRefreshRunner;
	bool				fServerRunning;
	int32				fConnectedCount;
	std::vector<StatusDevice> fDevices;
	BString				fToolTipText;
};


BluetoothStatusView*
BluetoothStatusView::Instantiate(BMessage* archive)
{
	return validate_instantiation(archive, "BluetoothStatusView")
		? new BluetoothStatusView(archive) : NULL;
}


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	return new BluetoothStatusView(BRect(0, 0, maxHeight - 1, maxHeight - 1),
		true);
}


// #pragma mark - BluetoothStatusApp


class BluetoothStatusApp : public BApplication {
public:
	BluetoothStatusApp()
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
		if (deskbar.IsRunning()) {
			// This applet supersedes the server's old replicant.
			if (deskbar.HasItem(kServerItemName))
				deskbar.RemoveItem(kServerItemName);
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
					fprintf(stderr, "BluetoothStatus: Deskbar install "
						"failed: %s\n", strerror(result));
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
	BluetoothStatusApp app;
	app.Run();
	return 0;
}
