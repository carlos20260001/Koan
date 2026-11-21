/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "WiFiController.h"

#include <errno.h>
#include <net/if.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <Catalog.h>
#include <InterfaceDefs.h>
#include <MessageRunner.h>
#include <NetworkInterface.h>
#include <NetworkNotifications.h>
#include <NetworkRoster.h>

#include <NetServer.h>

extern "C" {
#	include <freebsd_network/compat/sys/cdefs.h>
#	include <freebsd_network/compat/net/if_media.h>
}

#include <new>

#include "../../shared/net/WirelessNetworkList.h"
#include "LinuxWireless.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WiFiController"

#define QUOTED_NAME B_UTF8_OPEN_QUOTE "%name%" B_UTF8_CLOSE_QUOTE


static const uint32 kMsgTick = 'wfTk';

static const bigtime_t kTickInterval = 2000000;
static const bigtime_t kScanTimeout = 12000000;
	// For drivers that never report a completed scan
static const bigtime_t kScanRetryInterval = 20000000;
static const bigtime_t kJoinTimeout = 30000000;
static const bigtime_t kSavedRefreshInterval = 30000000;
static const bigtime_t kMinimumEntryAge = 180000000;
static const bigtime_t kMissedScanGrace = 50000000;
static const bigtime_t kNetServerDeliveryTimeout = 2000000;
static const bigtime_t kNetServerReplyTimeout = 8000000;
static const bigtime_t kTargetTimeout = 250000;
	// Never block on a busy target, it may be waiting for us to quit


// #pragma mark - WiFiNetworkInfo


WiFiNetworkInfo::WiFiNetworkInfo()
	:
	authentication(B_NETWORK_AUTHENTICATION_NONE),
	secured(false),
	dBm(-100),
	percent(0),
	bars(0),
	accessPoints(0),
	connected(false),
	saved(false),
	inRange(false)
{
}


status_t
WiFiNetworkInfo::Archive(BMessage& archive) const
{
	status_t status = archive.AddString("name", name);
	if (status == B_OK)
		status = archive.AddUInt32("authentication", authentication);
	if (status == B_OK)
		status = archive.AddBool("secured", secured);
	if (status == B_OK)
		status = archive.AddInt32("dbm", dBm);
	if (status == B_OK)
		status = archive.AddInt32("percent", percent);
	if (status == B_OK)
		status = archive.AddInt32("bars", bars);
	if (status == B_OK)
		status = archive.AddInt32("access points", accessPoints);
	if (status == B_OK)
		status = archive.AddBool("connected", connected);
	if (status == B_OK)
		status = archive.AddBool("saved", saved);
	if (status == B_OK)
		status = archive.AddBool("in range", inRange);
	return status;
}


status_t
WiFiNetworkInfo::Unarchive(const BMessage& archive)
{
	status_t status = archive.FindString("name", &name);
	if (status != B_OK)
		return status;

	authentication = archive.GetUInt32("authentication",
		B_NETWORK_AUTHENTICATION_NONE);
	secured = archive.GetBool("secured", false);
	dBm = archive.GetInt32("dbm", -100);
	percent = archive.GetInt32("percent", 0);
	bars = archive.GetInt32("bars", 0);
	accessPoints = archive.GetInt32("access points", 0);
	connected = archive.GetBool("connected", false);
	saved = archive.GetBool("saved", false);
	inRange = archive.GetBool("in range", false);
	return B_OK;
}


bool
WiFiNetworkInfo::operator==(const WiFiNetworkInfo& other) const
{
	return name == other.name && authentication == other.authentication
		&& secured == other.secured && dBm == other.dBm
		&& percent == other.percent && bars == other.bars
		&& accessPoints == other.accessPoints && connected == other.connected
		&& saved == other.saved && inRange == other.inRange;
}


// #pragma mark - WiFiLinkDetails


WiFiLinkDetails::WiFiLinkDetails()
	:
	channel(0),
	frequency(0),
	dBm(0),
	txKbps(0)
{
}


void
WiFiLinkDetails::Archive(BMessage& archive) const
{
	archive.AddString("bssid", bssid);
	archive.AddString("address", address);
	archive.AddString("router", router);
	archive.AddString("security", security);
	archive.AddString("phy", phyMode);
	archive.AddInt32("channel", channel);
	archive.AddInt32("frequency", frequency);
	archive.AddInt32("dbm", dBm);
	archive.AddInt32("tx kbps", txKbps);
	archive.AddString("tx details", txDetails);
}


void
WiFiLinkDetails::Unarchive(const BMessage& archive)
{
	bssid = archive.GetString("bssid", "");
	address = archive.GetString("address", "");
	router = archive.GetString("router", "");
	security = archive.GetString("security", "");
	phyMode = archive.GetString("phy", "");
	channel = archive.GetInt32("channel", 0);
	frequency = archive.GetInt32("frequency", 0);
	dBm = archive.GetInt32("dbm", 0);
	txKbps = archive.GetInt32("tx kbps", 0);
	txDetails = archive.GetString("tx details", "");
}


static int32
ChannelForFrequency(int32 frequency)
{
	if (frequency == 2484)
		return 14;
	if (frequency >= 2412 && frequency <= 2472)
		return (frequency - 2407) / 5;
	if (frequency >= 5955 && frequency <= 7115)
		return (frequency - 5950) / 5;
	if (frequency >= 5000 && frequency < 5950)
		return (frequency - 5000) / 5;
	return 0;
}


static const char*
PhyModeName(int32 media)
{
	if (IFM_TYPE(media) != IFM_IEEE80211)
		return NULL;
	switch (IFM_MODE(media)) {
		case IFM_IEEE80211_11A:
			return "802.11a";
		case IFM_IEEE80211_11B:
			return "802.11b";
		case IFM_IEEE80211_11G:
			return "802.11g";
		case IFM_IEEE80211_11NA:
		case IFM_IEEE80211_11NG:
			return "802.11n";
		case IFM_IEEE80211_VHT5G:
		case IFM_IEEE80211_VHT2G:
			return "802.11ac";
		case IFM_IEEE80211_HE5G:
		case IFM_IEEE80211_HE2G:
			return "802.11ax";
		default:
			return NULL;
	}
}


static BString
SecurityName(const wireless_network& network)
{
	BString name;
	switch (network.authentication_mode) {
		case B_NETWORK_AUTHENTICATION_NONE:
			return B_TRANSLATE("None");
		case B_NETWORK_AUTHENTICATION_WEP:
			return "WEP";
		case B_NETWORK_AUTHENTICATION_WPA:
			name = "WPA";
			break;
		case B_NETWORK_AUTHENTICATION_WPA2:
			name = "WPA2";
			break;
		case B_NETWORK_AUTHENTICATION_EAP:
			return B_TRANSLATE("WPA Enterprise");
		default:
			return B_TRANSLATE("Encrypted");
	}
	name << ((network.key_mode & B_KEY_MODE_IEEE802_1X) != 0
		? " Enterprise" : " Personal");
	if ((network.cipher & B_NETWORK_CIPHER_CCMP) != 0)
		name << " (AES)";
	else if ((network.cipher & B_NETWORK_CIPHER_TKIP) != 0)
		name << " (TKIP)";
	return name;
}


/*!	Asks the driver for the rate it last transmitted at. Linux's wireless
	extensions report the rate only; the HT/VHT/HE MCS details Haiku's
	driver extension gave stay empty.
*/
static void
ReadTxRate(BNetworkDevice& device, WiFiLinkDetails& link)
{
	// TODO(linux backend): the MCS, stream and channel-width details
	// (nl80211 NL80211_STA_INFO_TX_BITRATE) for link.txDetails.
	uint32 kbps = linux_wireless_get_tx_rate(device.Name());
	if (kbps != 0)
		link.txKbps = kbps;
}


/*!	Returns the channel frequency of \a bssid. Linux's wireless
	extensions report the frequency of the current association, which is
	the one asked for here.
*/
static int32
FrequencyForBSSID(BNetworkDevice& device, const BNetworkAddress& bssid)
{
	(void)bssid;
	return linux_wireless_get_frequency(device.Name());
}


/*!	Fills in what the radio reports about the link with \a bssid: the PHY
	mode, the channel and the last transmit rate.
*/
static void
ReadRadioDetails(BNetworkInterface& interface, BNetworkDevice& device,
	const BNetworkAddress& bssid, WiFiLinkDetails& link)
{
	const char* phy = PhyModeName(interface.Media());
	if (phy != NULL)
		link.phyMode = phy;
	link.frequency = FrequencyForBSSID(device, bssid);
	link.channel = ChannelForFrequency(link.frequency);
	ReadTxRate(device, link);
}


status_t
GetWiFiLinkDetails(const char* deviceName, WiFiLinkDetails& link,
	BString& networkName)
{
	link = WiFiLinkDetails();
	networkName = "";

	BNetworkInterface interface(deviceName);
	BNetworkDevice device(deviceName);
	if (!device.IsWireless())
		return B_BAD_TYPE;
	if ((interface.Flags() & IFF_UP) == 0 || !device.HasLink())
		return B_NOT_ALLOWED;

	BNetworkAddress bssid;
	uint32 cookie = 0;
	if (device.GetNextAssociatedNetwork(cookie, bssid) != B_OK)
		return B_ENTRY_NOT_FOUND;

	wireless_network* networks = NULL;
	uint32 count = 0;
	if (device.GetNetworks(networks, count) != B_OK) {
		networks = NULL;
		count = 0;
	}

	const wireless_network* associated = NULL;
	for (uint32 i = 0; i < count; i++) {
		if (networks[i].address == bssid && networks[i].name[0] != '\0') {
			associated = &networks[i];
			break;
		}
	}

	if (associated != NULL) {
		networkName.SetTo(associated->name, sizeof(associated->name));
		link.dBm = WirelessSignalDBm(*associated);

		// The associated access point's own record may lack its RSN
		// details; another one of the same network knows how it is secured.
		const wireless_network* secured = associated;
		for (uint32 i = 0; i < count; i++) {
			if (strncmp(networks[i].name, associated->name,
					sizeof(networks[i].name)) == 0
				&& networks[i].authentication_mode
					> secured->authentication_mode) {
				secured = &networks[i];
			}
		}
		link.security = SecurityName(*secured);
	} else {
		char ssid[32];
		ssize_t length = linux_wireless_get_ssid(deviceName, ssid,
			sizeof(ssid));
		if (length > 0)
			networkName.SetTo(ssid, length);
	}
	delete[] networks;

	int32 index = interface.FindFirstAddress(AF_INET);
	BNetworkInterfaceAddress address;
	if (index >= 0 && interface.GetAddressAt(index, address) == B_OK
		&& !address.Address().IsEmpty()
		&& !address.Address().IsWildcard()) {
		link.address = address.Address().ToString();
		BNetworkAddress gateway;
		if (interface.GetDefaultGateway(AF_INET, gateway) == B_OK)
			link.router = gateway.ToString();
	}

	link.bssid = bssid.ToString();
	ReadRadioDetails(interface, device, bssid, link);
	return B_OK;
}


// #pragma mark - WiFiState


WiFiState::WiFiState()
	:
	state(WIFI_STATE_NO_ADAPTER),
	scanning(false),
	savedValid(false),
	netServerRunning(true)
{
}


status_t
WiFiState::Archive(BMessage& archive) const
{
	status_t status = archive.AddString("device", device);
	for (size_t i = 0; status == B_OK && i < devices.size(); i++)
		status = archive.AddString("devices", devices[i]);
	if (status == B_OK)
		status = archive.AddInt32("state", state);
	if (status == B_OK)
		status = archive.AddString("current", current);
	if (status == B_OK)
		status = archive.AddString("pending", pending);
	if (status == B_OK)
		status = archive.AddString("address", address);
	if (status == B_OK)
		status = archive.AddBool("scanning", scanning);
	if (status == B_OK)
		status = archive.AddBool("saved valid", savedValid);
	if (status == B_OK)
		status = archive.AddBool("net_server", netServerRunning);
	if (status == B_OK) {
		BMessage details;
		link.Archive(details);
		status = archive.AddMessage("link", &details);
	}
	for (size_t i = 0; status == B_OK && i < networks.size(); i++) {
		BMessage network;
		status = networks[i].Archive(network);
		if (status == B_OK)
			status = archive.AddMessage("network", &network);
	}
	return status;
}


status_t
WiFiState::Unarchive(const BMessage& archive)
{
	device = archive.GetString("device", "");
	devices.clear();
	const char* name;
	for (int32 i = 0; archive.FindString("devices", i, &name) == B_OK; i++)
		devices.push_back(name);
	state = archive.GetInt32("state", WIFI_STATE_NO_ADAPTER);
	current = archive.GetString("current", "");
	pending = archive.GetString("pending", "");
	address = archive.GetString("address", "");
	scanning = archive.GetBool("scanning", false);
	savedValid = archive.GetBool("saved valid", false);
	netServerRunning = archive.GetBool("net_server", true);
	BMessage details;
	if (archive.FindMessage("link", &details) == B_OK)
		link.Unarchive(details);
	else
		link = WiFiLinkDetails();

	networks.clear();
	BMessage network;
	for (int32 i = 0; archive.FindMessage("network", i, &network) == B_OK;
			i++) {
		WiFiNetworkInfo info;
		if (info.Unarchive(network) == B_OK)
			networks.push_back(info);
	}
	return B_OK;
}


const WiFiNetworkInfo*
WiFiState::FindNetwork(const char* name) const
{
	if (name == NULL || name[0] == '\0')
		return NULL;

	for (size_t i = 0; i < networks.size(); i++) {
		if (networks[i].name == name)
			return &networks[i];
	}
	return NULL;
}


bool
WiFiState::IsAssociated() const
{
	return state == WIFI_STATE_CONNECTED || state == WIFI_STATE_NO_INTERNET
		|| state == WIFI_STATE_OBTAINING_ADDRESS;
}


// #pragma mark - WiFiController


WiFiController::WiFiController(const BMessenger& target,
	const char* component, bigtime_t scanInterval)
	:
	BLooper("WiFi controller"),
	fTarget(target),
	fComponent(component),
	fRunner(NULL),
	fWatching(false),
	fSavedValid(false),
	fSavedUpdated(0),
	fNetServerRunning(true),
	fScanInterval(scanInterval),
	fScanPending(false),
	fScanRequested(0),
	fLastScan(0),
	fAutoJoinAfterScan(false),
	fJoinPending(false),
	fJoinAuthentication(B_NETWORK_AUTHENTICATION_NONE),
	fJoinSecured(false),
	fJoinHasPassword(false),
	fJoinRemember(false),
	fJoinStarted(0),
	fJoinSawOther(false),
	fHasPostedState(false)
{
}


WiFiController::~WiFiController()
{
	delete fRunner;
	if (fWatching)
		BNetworkRoster::Default().StopWatching(BMessenger(this));

	// Do not keep a password in memory longer than necessary
	fJoinPassword.SetTo('\0', fJoinPassword.Length());
}


status_t
WiFiController::Start()
{
	thread_id thread = Run();
	if (thread < 0)
		return thread;

	BMessenger messenger(this);
	if (BNetworkRoster::Default().StartWatching(messenger,
			B_WATCH_NETWORK_INTERFACE_CHANGES | B_WATCH_NETWORK_LINK_CHANGES
				| B_WATCH_NETWORK_WLAN_CHANGES) == B_OK) {
		fWatching = true;
	}

	BMessage tick(kMsgTick);
	fRunner = new BMessageRunner(messenger, &tick, kTickInterval);

	WiFiDebugLog(fComponent, "controller started, scan interval %" B_PRId64
		" s", fScanInterval / 1000000);
	PostMessage(kMsgWiFiRefresh);
	return B_OK;
}


void
WiFiController::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTick:
			_Update(fScanPending
				&& system_time() - fScanRequested >= kScanTimeout);
			break;

		case kMsgWiFiRefresh:
			_UpdateSaved(true);
			_Update(false);
			break;

		case kMsgWiFiScan:
		{
			bigtime_t maxAge;
			if (fJoinPending || (message->FindInt64("max_age", &maxAge)
						== B_OK
					&& (fScanPending || system_time() - fLastScan < maxAge))) {
				break;
			}
			_StartScan("requested");
			_Update(false);
			break;
		}

		case kMsgWiFiJoin:
			_Join(message);
			break;

		case kMsgWiFiLeave:
			_Leave();
			break;

		case kMsgWiFiForget:
		{
			const char* name;
			if (message->FindString("name", &name) == B_OK)
				_Forget(name);
			break;
		}

		case kMsgWiFiSetPower:
			_SetPower(message->GetBool("on", true));
			break;

		case kMsgWiFiSelectDevice:
		{
			const char* device;
			if (message->FindString("device", &device) == B_OK
				&& fDevice != device) {
				fRequestedDevice = device;
				_Update(false);
			}
			break;
		}

		case kMsgWiFiSetScanInterval:
			fScanInterval = message->GetInt64("interval", fScanInterval);
			break;

		case B_NETWORK_MONITOR:
		{
			int32 opcode = message->GetInt32("opcode", 0);
			const char* interface = message->GetString("interface", "");
			// Notifications may name the interface without the /dev/ prefix
			bool ours = !fDevice.IsEmpty() && interface[0] != '\0'
				&& fDevice.FindFirst(interface) >= 0;
			if (opcode == B_NETWORK_WLAN_SCANNED) {
				if (ours) {
					WiFiDebugLog(fComponent, "%s: scan completed",
						fDevice.String());
					_Update(true);
				}
			} else if (ours || opcode == B_NETWORK_INTERFACE_ADDED
				|| opcode == B_NETWORK_INTERFACE_REMOVED) {
				if (opcode == B_NETWORK_WLAN_JOINED
					|| opcode == B_NETWORK_WLAN_LEFT) {
					WiFiDebugLog(fComponent, "%s: %s", fDevice.String(),
						opcode == B_NETWORK_WLAN_JOINED ? "joined" : "left");
				}
				_Update(false);
			}
			break;
		}

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


void
WiFiController::_Update(bool scanCompleted)
{
	bool deviceChanged = false;
	if (!_ChooseDevice(deviceChanged)) {
		if (fJoinPending)
			_FinishJoin(B_ENTRY_NOT_FOUND, B_TRANSLATE("The Wi-Fi adapter "
				"is no longer available."));
		_ClearNetworks();
		fScanPending = false;
		fState = WiFiState();
		fState.devices = fDevices;
		fState.state = WIFI_STATE_NO_ADAPTER;
		fState.netServerRunning = fNetServerRunning;
		_PostState();
		return;
	}
	if (deviceChanged) {
		WiFiDebugLog(fComponent, "using adapter %s", fDevice.String());
		if (fJoinPending)
			_FinishJoin(B_CANCELED, NULL);
		_ClearNetworks();
		fScanPending = false;
		fLastScan = 0;
	}

	BNetworkInterface interface(fDevice.String());
	BNetworkDevice device(fDevice.String());
	bool powered = (interface.Flags() & IFF_UP) != 0;

	WiFiState state;
	state.device = fDevice;
	state.devices = fDevices;

	_UpdateSaved(false);
	state.savedValid = fSavedValid;
	state.netServerRunning = fNetServerRunning;

	if (!powered) {
		if (fJoinPending)
			_FinishJoin(B_CANCELED, B_TRANSLATE("Wi-Fi was turned off."));
		_ClearNetworks();
		fScanPending = false;
		state.state = WIFI_STATE_OFF;
		for (size_t i = 0; i < fSaved.size(); i++) {
			WiFiNetworkInfo info;
			info.name = fSaved[i].name;
			info.authentication = fSaved[i].authentication;
			info.secured = info.authentication != B_NETWORK_AUTHENTICATION_NONE;
			info.saved = true;
			state.networks.push_back(info);
		}
		fState = state;
		_PostState();
		return;
	}

	// Find out what we are associated with. Look the access point up in the
	// scan results: asking the driver for station details is not supported
	// by all drivers.

	wireless_network* networks = NULL;
	uint32 count = 0;
	status_t status = device.GetNetworks(networks, count);
	if (status != B_OK) {
		WiFiDebugLog(fComponent, "%s: reading scan results failed: %s",
			fDevice.String(), strerror(status));
		networks = NULL;
		count = 0;
	}

	BString associatedName;
	const wireless_network* associated = NULL;
	BNetworkAddress bssid;
	uint32 cookie = 0;
	if (device.GetNextAssociatedNetwork(cookie, bssid) == B_OK) {
		for (uint32 i = 0; i < count; i++) {
			if (networks[i].address == bssid && networks[i].name[0] != '\0') {
				associated = &networks[i];
				associatedName.SetTo(associated->name,
					sizeof(associated->name));
				break;
			}
		}
		if (associated == NULL) {
			// Not in the scan results, ask for the SSID
			char ssid[32];
			ssize_t length = linux_wireless_get_ssid(device.Name(), ssid,
				sizeof(ssid));
			if (length > 0)
				associatedName.SetTo(ssid, length);
		}
	}
	bool link = device.HasLink();

	BString associatedSecurity;
	int32 associatedDBm = 0;
	if (associated != NULL) {
		associatedSecurity = SecurityName(*associated);
		associatedDBm = WirelessSignalDBm(*associated);
	}

	_ReadNetworks(scanCompleted, networks, count, associatedName,
		associated);
	delete[] networks;

	// The associated access point's own record may lack its RSN details;
	// the scan group for the same name knows how the network is secured.
	if (!associatedName.IsEmpty()) {
		const Entry* group = _FindEntry(associatedName);
		if (group != NULL && (associatedSecurity.IsEmpty()
				|| group->network.authentication_mode
					> associated->authentication_mode)) {
			associatedSecurity = SecurityName(group->network);
		}
		if (associatedSecurity.IsEmpty()) {
			const SavedNetwork* saved = _FindSaved(associatedName);
			if (saved != NULL) {
				wireless_network network = {};
				network.authentication_mode = saved->authentication;
				network.cipher = B_NETWORK_CIPHER_CCMP;
				associatedSecurity = SecurityName(network);
			}
		}
	}

	_CheckJoin(!associatedName.IsEmpty(), link, associatedName);

	if (fJoinPending) {
		state.state = WIFI_STATE_CONNECTING;
		state.current = fJoinName;
		state.pending = fJoinName;
	} else if (!associatedName.IsEmpty() && link) {
		state.current = associatedName;
		state.state = WIFI_STATE_OBTAINING_ADDRESS;
		int32 index = interface.FindFirstAddress(AF_INET);
		BNetworkInterfaceAddress address;
		if (index >= 0 && interface.GetAddressAt(index, address) == B_OK
			&& !address.Address().IsEmpty()
			&& !address.Address().IsWildcard()) {
			state.address = address.Address().ToString();
			BNetworkAddress gateway;
			bool hasGateway
				= interface.GetDefaultGateway(AF_INET, gateway) == B_OK;
			state.state = hasGateway
				? WIFI_STATE_CONNECTED : WIFI_STATE_NO_INTERNET;
			state.link.address = state.address;
			if (hasGateway)
				state.link.router = gateway.ToString();
		}
		state.link.bssid = bssid.ToString();
		state.link.security = associatedSecurity;
		state.link.dBm = associatedDBm;
		ReadRadioDetails(interface, device, bssid, state.link);
	} else if (!associatedName.IsEmpty()) {
		state.state = WIFI_STATE_CONNECTING;
		state.current = associatedName;
	} else
		state.state = WIFI_STATE_DISCONNECTED;

	state.scanning = fScanPending;

	// Build the list: networks in range by signal, then saved ones

	for (size_t i = 0; i < fEntries.size(); i++) {
		const Entry& entry = fEntries[i];
		WiFiNetworkInfo info;
		info.name.SetTo(entry.network.name, sizeof(entry.network.name));
		info.authentication = entry.network.authentication_mode;
		info.secured = entry.secured;
		info.dBm = WirelessSignalDBm(entry.network);
		info.percent = WirelessSignalPercent(entry.network);
		info.bars = WirelessSignalBars(entry.network);
		info.accessPoints = entry.accessPoints;
		info.connected = !state.current.IsEmpty() && info.name == state.current;
		info.saved = _FindSaved(info.name) != NULL;
		info.inRange = true;
		state.networks.push_back(info);
	}
	std::stable_sort(state.networks.begin(), state.networks.end(),
		[](const WiFiNetworkInfo& a, const WiFiNetworkInfo& b) {
			return a.dBm > b.dBm;
		});
	for (size_t i = 0; i < fSaved.size(); i++) {
		if (state.FindNetwork(fSaved[i].name) != NULL)
			continue;
		WiFiNetworkInfo info;
		info.name = fSaved[i].name;
		info.authentication = fSaved[i].authentication;
		info.secured = info.authentication != B_NETWORK_AUTHENTICATION_NONE;
		info.saved = true;
		info.connected = info.name == state.current;
		state.networks.push_back(info);
	}

	fState = state;
	_PostState();

	if (scanCompleted && fAutoJoinAfterScan) {
		fAutoJoinAfterScan = false;
		if (associatedName.IsEmpty() && !fJoinPending)
			_AutoJoin();
	}

	_MaybeScan(!associatedName.IsEmpty());
}


/*!	Picks the wireless adapter to use: the one the user chose, or else the
	previous one, or else the first associated one, or else the first one.
	Returns false when there is none.
*/
bool
WiFiController::_ChooseDevice(bool& changed)
{
	std::vector<BString> devices;
	BString associated;
	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		BNetworkDevice device(interface.Name());
		if (!device.IsWireless())
			continue;
		devices.push_back(interface.Name());
		BNetworkAddress address;
		uint32 associationCookie = 0;
		if (associated.IsEmpty() && device.HasLink()
			&& device.GetNextAssociatedNetwork(associationCookie, address)
				== B_OK) {
			associated = interface.Name();
		}
	}

	if (devices.size() != fDevices.size()) {
		WiFiDebugLog(fComponent, "%zu wireless adapter(s) present",
			devices.size());
	}
	fDevices = devices;

	BString chosen;
	for (size_t i = 0; i < devices.size(); i++) {
		if (!fRequestedDevice.IsEmpty() && devices[i] == fRequestedDevice) {
			chosen = fRequestedDevice;
			break;
		}
	}
	if (chosen.IsEmpty()) {
		for (size_t i = 0; i < devices.size(); i++) {
			if (devices[i] == fDevice) {
				chosen = fDevice;
				break;
			}
		}
	}
	if (chosen.IsEmpty())
		chosen = !associated.IsEmpty() ? associated
			: devices.empty() ? BString() : devices[0];

	changed = chosen != fDevice;
	fDevice = chosen;
	return !fDevice.IsEmpty();
}


void
WiFiController::_ClearNetworks()
{
	fEntries.clear();
}


/*!	Merges the adapter's scan cache into the network list.

	The cache may be flushed while a scan is running, and some drivers replace
	it with just the associated access point. Networks are therefore only
	removed after a completed scan that does not contain them, or when they
	have not been seen for a long time.
*/
void
WiFiController::_ReadNetworks(bool scanCompleted,
	const wireless_network* networks, uint32 count,
	const BString& associatedName, const wireless_network* associated)
{
	if (scanCompleted) {
		fScanPending = false;
		fLastScan = system_time();
	}

	std::vector<WirelessNetworkGroup> groups = GroupWirelessNetworks(networks,
		count, associatedName.String());

	if (scanCompleted)
		LogWirelessScan(fComponent, fDevice, networks, count, groups);
	else if (WiFiDebugEnabled()) {
		BString summary;
		summary << count << "/" << (int32)groups.size();
		if (summary != fLoggedSummary) {
			WiFiDebugLog(fComponent, "%s: cache now has %" B_PRIu32
				" access points in %zu networks", fDevice.String(), count,
				groups.size());
		}
		fLoggedSummary = summary;
	}

	bigtime_t now = system_time();
	// A completed scan that saw a single access point at most is likely
	// just the driver's view of the current association.
	bool replace = scanCompleted && count > 1;

	for (size_t i = 0; i < groups.size(); i++) {
		const WirelessNetworkGroup& group = groups[i];
		Entry* entry = _FindEntry(group.network.name);
		if (entry == NULL) {
			Entry newEntry;
			newEntry.network = group.network;
			newEntry.accessPoints = group.accessPoints;
			newEntry.secured = group.secured;
			newEntry.lastSeen = now;
			fEntries.push_back(newEntry);
			continue;
		}

		entry->lastSeen = now;
		if (replace || group.accessPoints >= entry->accessPoints
			|| group.network.address == entry->network.address) {
			// A complete view of the group, or an update of the access point
			// it is represented by.
			bool secured = entry->secured;
			uint32 authentication = entry->network.authentication_mode;
			entry->network = group.network;
			entry->accessPoints = replace ? group.accessPoints
				: std::max(entry->accessPoints, group.accessPoints);
			entry->secured = replace ? group.secured
				: secured || group.secured;
			if (!replace && entry->network.authentication_mode
					== B_NETWORK_AUTHENTICATION_NONE)
				entry->network.authentication_mode = authentication;
		} else {
			entry->secured |= group.secured;
		}
	}

	bigtime_t maximumAge = std::max(kMinimumEntryAge, 3 * fScanInterval);
	for (int32 i = (int32)fEntries.size() - 1; i >= 0; i--) {
		Entry& entry = fEntries[i];
		if (entry.lastSeen == now)
			continue;
		BString name(entry.network.name, sizeof(entry.network.name));
		// Weak networks come and go between scans: only drop them when
		// they were missed by more than one scan.
		bool missed = replace && now - entry.lastSeen > kMissedScanGrace;
		if (missed || now - entry.lastSeen > maximumAge) {
			WiFiDebugLog(fComponent, "dropping \"%s\" (%s)", name.String(),
				missed ? "missed by completed scans" : "not seen recently");
			fEntries.erase(fEntries.begin() + i);
		}
	}

	if (!associatedName.IsEmpty() && _FindEntry(associatedName) == NULL
		&& associated != NULL) {
		// Always show the network we are associated with
		Entry entry;
		entry.network = *associated;
		entry.accessPoints = 1;
		entry.secured = WirelessNetworkIsSecured(*associated);
		entry.lastSeen = now;
		fEntries.push_back(entry);
	}
}


void
WiFiController::_UpdateSaved(bool force)
{
	bigtime_t now = system_time();
	if (!force && fSavedValid && now - fSavedUpdated < kSavedRefreshInterval)
		return;
	if (!force && !fSavedValid && now - fSavedUpdated < kTickInterval * 2)
		return;
	fSavedUpdated = now;

	BMessage request(kMsgCountPersistentNetworks);
	BMessage reply;
	status_t status = _SendToNetServer(request, reply,
		kNetServerReplyTimeout);
	int32 count = 0;
	if (status == B_OK && reply.FindInt32("count", &count) != B_OK)
		status = B_BAD_DATA;
	if (status != B_OK) {
		if (fNetServerRunning || fSavedValid) {
			WiFiDebugLog(fComponent, "could not list saved networks: %s",
				strerror(status));
		}
		fNetServerRunning = false;
		// Keep the previous list, net_server may just be restarting
		return;
	}
	fNetServerRunning = true;

	std::vector<SavedNetwork> saved;
	for (int32 i = 0; i < count; i++) {
		BMessage get(kMsgGetPersistentNetwork);
		get.AddInt32("index", i);
		BMessage getReply;
		status = _SendToNetServer(get, getReply, kNetServerReplyTimeout);
		if (status == B_OK)
			status = getReply.GetInt32("status", B_ERROR);
		BMessage network;
		if (status == B_OK)
			status = getReply.FindMessage("network", &network);
		if (status != B_OK)
			break;

		// The settings may contain a password: only the name and the
		// security type are taken from it.
		SavedNetwork entry;
		if (network.FindString("name", &entry.name) != B_OK
			|| entry.name.IsEmpty()) {
			continue;
		}
		uint32 authentication;
		if (network.FindUInt32("authentication_mode", &authentication)
				!= B_OK) {
			authentication = B_NETWORK_AUTHENTICATION_NONE;
			const char* mode;
			if (network.FindString("authentication", &mode) == B_OK) {
				if (strcasecmp(mode, "wpa2") == 0)
					authentication = B_NETWORK_AUTHENTICATION_WPA2;
				else if (strcasecmp(mode, "wpa") == 0)
					authentication = B_NETWORK_AUTHENTICATION_WPA;
				else if (strcasecmp(mode, "wep") == 0)
					authentication = B_NETWORK_AUTHENTICATION_WEP;
			}
		}
		entry.authentication = authentication;
		saved.push_back(entry);
	}
	if (status != B_OK) {
		WiFiDebugLog(fComponent, "could not read saved network: %s",
			strerror(status));
		return;
	}

	if (!fSavedValid || saved.size() != fSaved.size())
		WiFiDebugLog(fComponent, "%zu saved network(s)", saved.size());
	fSaved = saved;
	fSavedValid = true;
}


status_t
WiFiController::_StartScan(const char* reason)
{
	BNetworkInterface interface(fDevice.String());
	if (fDevice.IsEmpty() || (interface.Flags() & IFF_UP) == 0) {
		// Scanning would bring the radio back up
		return B_NOT_ALLOWED;
	}

	BNetworkDevice device(fDevice.String());
	status_t status = device.Scan(false, true);
	fScanRequested = system_time();
	if (status != B_OK) {
		WiFiDebugLog(fComponent, "%s: scan (%s) failed: %s", fDevice.String(),
			reason, strerror(status));
		return status;
	}

	WiFiDebugLog(fComponent, "%s: scan started (%s)", fDevice.String(),
		reason);
	fScanPending = true;
	fState.scanning = true;
	return B_OK;
}


void
WiFiController::_MaybeScan(bool associated)
{
	// Do not disturb an association in progress
	if (fScanPending || fJoinPending)
		return;

	// Without a periodic interval, still look for networks while
	// disconnected
	bigtime_t interval = fScanInterval;
	if (interval <= 0 && !associated)
		interval = 60000000;
	if (interval <= 0)
		return;

	bigtime_t now = system_time();
	if (fLastScan != 0 && now - fLastScan < interval)
		return;
	if (fScanRequested != 0 && now - fScanRequested < kScanRetryInterval)
		return;

	if (_StartScan(fLastScan == 0 ? "initial" : "periodic") == B_OK)
		_PostState();
}


void
WiFiController::_CheckJoin(bool associated, bool link, const BString& name)
{
	if (!fJoinPending)
		return;

	if (associated && link && name == fJoinName) {
		WiFiDebugLog(fComponent, "joined \"%s\" after %" B_PRId64 " ms",
			fJoinName.String(), (system_time() - fJoinStarted) / 1000);

		if (fJoinRemember && (fJoinHasPassword || !fJoinSecured)) {
			// Only remember credentials that were proven to work
			BMessage add(kMsgAddPersistentNetwork);
			add.AddString("name", fJoinName);
			if (fJoinHasPassword)
				add.AddString("password", fJoinPassword);
			add.AddUInt32("flags", fJoinSecured ? B_NETWORK_IS_ENCRYPTED : 0);
			add.AddUInt32("authentication_mode", fJoinAuthentication);
			bool wpa = fJoinAuthentication == B_NETWORK_AUTHENTICATION_WPA2
				|| fJoinAuthentication == B_NETWORK_AUTHENTICATION_WPA;
			uint32 cipher = fJoinAuthentication
					== B_NETWORK_AUTHENTICATION_WPA2 ? B_NETWORK_CIPHER_CCMP
				: fJoinAuthentication == B_NETWORK_AUTHENTICATION_WPA
					? B_NETWORK_CIPHER_TKIP : B_NETWORK_CIPHER_NONE;
			add.AddUInt32("cipher", cipher);
			add.AddUInt32("group_cipher", cipher);
			add.AddUInt32("key_mode", wpa ? B_KEY_MODE_PSK : B_KEY_MODE_NONE);

			BMessage reply;
			status_t status = _SendToNetServer(add, reply,
				kNetServerReplyTimeout);
			if (status == B_OK)
				status = reply.GetInt32("status", B_ERROR);
			WiFiDebugLog(fComponent, "remembering \"%s\": %s",
				fJoinName.String(), strerror(status));
			add.MakeEmpty();
			_UpdateSaved(true);
			if (status != B_OK) {
				BString error(B_TRANSLATE("Connected, but the network could "
					"not be remembered: %error%"));
				error.ReplaceFirst("%error%", strerror(status));
				_FinishJoin(B_OK, error);
				return;
			}
		}
		_FinishJoin(B_OK, NULL);
		return;
	}

	if (associated && !name.IsEmpty() && name != fJoinName)
		fJoinSawOther = true;

	if (system_time() - fJoinStarted < kJoinTimeout)
		return;

	WiFiDebugLog(fComponent, "joining \"%s\" timed out (associated %d, link "
		"%d)", fJoinName.String(), associated, link);

	BString error;
	if (fJoinSecured && (associated && name == fJoinName)) {
		error = B_TRANSLATE("Could not connect to " QUOTED_NAME ". "
			"The password may be incorrect.");
	} else if (fJoinSecured && fJoinHasPassword) {
		error = B_TRANSLATE("Could not connect to " QUOTED_NAME ". "
			"Check the password and try again.");
	} else if (fJoinSecured && _FindSaved(fJoinName) == NULL) {
		error = B_TRANSLATE("Could not connect to " QUOTED_NAME ". "
			"A password is required.");
	} else {
		error = B_TRANSLATE("Could not connect to " QUOTED_NAME ". "
			"The network did not respond.");
	}
	error.ReplaceAll("%name%", fJoinName);
	_FinishJoin(B_TIMED_OUT, error);
}


void
WiFiController::_Join(BMessage* message)
{
	const char* name;
	if (message->FindString("name", &name) != B_OK || name[0] == '\0')
		return;

	if (fJoinPending)
		_FinishJoin(B_CANCELED, NULL);

	fJoinName = name;
	fJoinPassword = message->GetString("password", "");
	fJoinHasPassword = !fJoinPassword.IsEmpty();
	fJoinRemember = message->GetBool("remember", true);
	fJoinSawOther = false;

	// Security: the caller's choice, else the saved network's, else the scan's
	uint32 authentication = B_NETWORK_AUTHENTICATION_NONE;
	bool haveAuthentication = message->FindUInt32("authentication",
		&authentication) == B_OK;
	const SavedNetwork* saved = _FindSaved(name);
	Entry* entry = _FindEntry(name);
	bool secured = entry != NULL ? entry->secured : false;
	if (!haveAuthentication && saved != NULL
		&& saved->authentication != B_NETWORK_AUTHENTICATION_NONE) {
		authentication = saved->authentication;
		haveAuthentication = true;
	}
	if (!haveAuthentication && entry != NULL
		&& entry->network.authentication_mode
			!= B_NETWORK_AUTHENTICATION_NONE) {
		authentication = entry->network.authentication_mode;
		haveAuthentication = true;
	}
	if (haveAuthentication)
		secured = authentication != B_NETWORK_AUTHENTICATION_NONE;
	else if (secured && fJoinHasPassword) {
		// Encrypted, but the details are unknown: WPA2 is by far the most
		// likely.
		authentication = B_NETWORK_AUTHENTICATION_WPA2;
		haveAuthentication = true;
	}
	fJoinAuthentication = authentication;
	fJoinSecured = secured;

	BNetworkInterface interface(fDevice.String());
	if (fDevice.IsEmpty() || !interface.Exists()) {
		BString error(B_TRANSLATE("There is no Wi-Fi adapter."));
		fJoinPending = true;
		_FinishJoin(B_ENTRY_NOT_FOUND, error);
		return;
	}
	if ((interface.Flags() & IFF_UP) == 0)
		interface.SetFlags(interface.Flags() | IFF_UP);

	if (secured && !fJoinHasPassword && saved == NULL) {
		// Without a password, the supplicant would pop up its own dialog.
		BString error(B_TRANSLATE(QUOTED_NAME " requires a password."));
		error.ReplaceAll("%name%", name);
		fJoinPending = true;
		_FinishJoin(B_NOT_ALLOWED, error);
		return;
	}

	// Always join by name, never pin an access point: the supplicant and the
	// driver pick the best access point of a mesh.
	BMessage join(kMsgJoinNetwork);
	join.AddString("device", fDevice);
	join.AddString("name", name);
	const char* keyword = haveAuthentication
		? WirelessAuthenticationKeyword(authentication) : NULL;
	if (keyword != NULL)
		join.AddString("authentication", keyword);
	if (fJoinHasPassword && secured)
		join.AddString("password", fJoinPassword);

	WiFiDebugLog(fComponent, "joining \"%s\" on %s: authentication %s, %s, "
		"%s", name, fDevice.String(), keyword != NULL ? keyword : "(default)",
		fJoinHasPassword ? "new password" : saved != NULL
			? "saved credentials" : "no password",
		fJoinRemember ? "remember" : "do not remember");

	BMessage reply;
	status_t status = _SendToNetServer(join, reply, kNetServerReplyTimeout);
	if (status == B_OK)
		status = reply.GetInt32("status", B_ERROR);
	join.MakeEmpty();

	fJoinPending = true;
	fJoinStarted = system_time();
	if (status != B_OK) {
		BString error;
		if (status == B_TIMED_OUT || status == B_NAME_NOT_FOUND
			|| status == B_BAD_PORT_ID) {
			error = B_TRANSLATE("Could not connect to " QUOTED_NAME ": "
				"the network service is not responding.");
		} else {
			error = B_TRANSLATE("Could not connect to " QUOTED_NAME
				": %error%");
			error.ReplaceAll("%error%", strerror(status));
		}
		error.ReplaceAll("%name%", name);
		_FinishJoin(status, error);
		return;
	}

	_Update(false);
}


void
WiFiController::_Leave()
{
	if (fJoinPending)
		_FinishJoin(B_CANCELED, NULL);
	if (fDevice.IsEmpty())
		return;

	BMessage leave(kMsgLeaveNetwork);
	leave.AddString("device", fDevice);
	if (!fState.current.IsEmpty())
		leave.AddString("name", fState.current);
	BMessage reply;
	status_t status = _SendToNetServer(leave, reply, kNetServerReplyTimeout);
	if (status == B_OK)
		status = reply.GetInt32("status", B_ERROR);
	WiFiDebugLog(fComponent, "leaving \"%s\": %s", fState.current.String(),
		strerror(status));
	_Update(false);
}


void
WiFiController::_Forget(const char* name)
{
	BMessage remove(kMsgRemovePersistentNetwork);
	remove.AddString("name", name);
	BMessage reply;
	status_t status = _SendToNetServer(remove, reply, kNetServerReplyTimeout);
	if (status == B_OK)
		status = reply.GetInt32("status", B_ERROR);
	WiFiDebugLog(fComponent, "forgetting \"%s\": %s", name, strerror(status));

	_UpdateSaved(true);
	_Update(false);

	if (status != B_OK) {
		BMessage result(kMsgWiFiJoinResult);
		result.AddString("name", name);
		result.AddInt32("status", status);
		BString error(B_TRANSLATE("Could not forget " QUOTED_NAME
			": %error%"));
		error.ReplaceAll("%name%", name);
		error.ReplaceAll("%error%", strerror(status));
		result.AddString("error", error);
		fTarget.SendMessage(&result, (BHandler*)NULL, kTargetTimeout);
	}
}


void
WiFiController::_SetPower(bool on)
{
	BNetworkInterface interface(fDevice.String());
	if (fDevice.IsEmpty() || !interface.Exists())
		return;

	uint32 flags = interface.Flags();
	if (on == ((flags & IFF_UP) != 0)) {
		_Update(false);
		return;
	}

	if (!on) {
		if (fJoinPending)
			_FinishJoin(B_CANCELED, NULL);
		if (!fState.current.IsEmpty())
			_Leave();
	}

	status_t status = interface.SetFlags(on ? flags | IFF_UP
		: flags & ~IFF_UP);
	WiFiDebugLog(fComponent, "turning %s %s: %s", fDevice.String(),
		on ? "on" : "off", strerror(status));

	if (on && status == B_OK) {
		// Look around, and join the best saved network, as after booting
		fAutoJoinAfterScan = true;
		fLastScan = 0;
		fScanRequested = 0;
		_StartScan("turned on");
	}
	_Update(false);
}


void
WiFiController::_AutoJoin()
{
	// fEntries are not sorted, pick the strongest saved network
	const Entry* best = NULL;
	for (size_t i = 0; i < fEntries.size(); i++) {
		const Entry& entry = fEntries[i];
		if (_FindSaved(entry.network.name) == NULL)
			continue;
		if (best == NULL
			|| entry.network.signal_strength > best->network.signal_strength)
			best = &entry;
	}
	if (best == NULL) {
		WiFiDebugLog(fComponent, "no saved network in range to join");
		return;
	}

	BMessage join(kMsgWiFiJoin);
	join.AddString("name", BString(best->network.name,
		sizeof(best->network.name)));
	join.AddBool("remember", false);
	_Join(&join);
}


void
WiFiController::_FinishJoin(status_t status, const char* error)
{
	if (!fJoinPending)
		return;

	fJoinPending = false;
	fJoinPassword.SetTo('\0', fJoinPassword.Length());
	fJoinPassword = "";
	fJoinHasPassword = false;

	if (status != B_OK && status != B_CANCELED)
		WiFiDebugLog(fComponent, "join of \"%s\" failed: %s",
			fJoinName.String(), error != NULL ? error : strerror(status));

	BMessage result(kMsgWiFiJoinResult);
	result.AddString("name", fJoinName);
	result.AddInt32("status", status);
	if (error != NULL)
		result.AddString("error", error);
	fTarget.SendMessage(&result, (BHandler*)NULL, kTargetTimeout);
}


void
WiFiController::_PostState()
{
	BMessage message(kMsgWiFiStateChanged);
	if (fState.Archive(message) == B_OK)
		fTarget.SendMessage(&message, (BHandler*)NULL, kTargetTimeout);
}


const WiFiController::SavedNetwork*
WiFiController::_FindSaved(const char* name) const
{
	for (size_t i = 0; i < fSaved.size(); i++) {
		if (fSaved[i].name == name)
			return &fSaved[i];
	}
	return NULL;
}


WiFiController::Entry*
WiFiController::_FindEntry(const char* name)
{
	for (size_t i = 0; i < fEntries.size(); i++) {
		if (WirelessNetworkNameEquals(fEntries[i].network.name, name))
			return &fEntries[i];
	}
	return NULL;
}


/*static*/ status_t
WiFiController::_SendToNetServer(BMessage& message, BMessage& reply,
	bigtime_t timeout)
{
	BMessenger netServer(kNetServerSignature);
	if (!netServer.IsValid())
		return B_NAME_NOT_FOUND;

	return netServer.SendMessage(&message, &reply, kNetServerDeliveryTimeout,
		timeout);
}
