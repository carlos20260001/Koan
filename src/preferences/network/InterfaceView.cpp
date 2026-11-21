/*
 * Copyright 2004-2026 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, <axeld@pinc-software.de>
 *		Alexander von Gluck, kallisti5@unixzen.com
 *		John Scipione, jscipione@gmail.com
 */


#include "InterfaceView.h"

#include <set>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if_media.h>
#include <net/route.h>
#include <netinet/in.h>
#include <resolv.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <AutoDeleter.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <LayoutUtils.h>
#include <MenuField.h>
#include <NetworkAddress.h>
#include <NetworkDevice.h>
#include <SeparatorView.h>
#include <StringForSize.h>
#include <StringView.h>
#include <Window.h>

#include "InterfaceStatus.h"
#include "MediaTypes.h"
#include "ThroughputView.h"
#include "WiFiController.h"
#include "WirelessNetworkMenuItem.h"
#include "../../shared/net/WirelessNetworkList.h"


static const uint32 kMsgInterfaceToggle = 'onof';
static const uint32 kMsgInterfaceRenegotiate = 'redo';
static const uint32 kMsgJoinNetwork = 'join';

static const bigtime_t kPulseRate = 1000000;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "IntefaceView"


// #pragma mark - StatusField


/*!	The status text with a coloured dot in front of it, like the status
	of a service in the macOS network settings.
*/
class StatusField : public BView {
public:
	StatusField()
		:
		BView("status field", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fColor(make_color(128, 128, 128))
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
		SetHighUIColor(B_PANEL_TEXT_COLOR);
		SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_MIDDLE));
	}

	void SetStatus(const char* text, rgb_color color)
	{
		if (fText == text && fColor == color)
			return;
		bool resize = fText != text;
		fText = text;
		fColor = color;
		if (resize)
			InvalidateLayout();
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		font_height height;
		GetFontHeight(&height);
		BRect bounds = Bounds();
		float baseline = floorf((bounds.top + bounds.bottom
			+ height.ascent - height.descent) / 2);

		float size = _DotSize();
		float centerY = baseline - floorf(height.ascent * 0.35f) - 0.5f;
		BRect dot(0, centerY - size / 2, size, centerY + size / 2);

		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(fColor);
		FillEllipse(dot);
		SetHighColor(tint_color(fColor, B_DARKEN_2_TINT));
		StrokeEllipse(dot);
		SetDrawingMode(B_OP_OVER);

		SetHighUIColor(B_PANEL_TEXT_COLOR);
		DrawString(fText, BPoint(size + be_control_look->DefaultLabelSpacing(),
			baseline));
	}

	virtual BSize MinSize()
	{
		font_height height;
		GetFontHeight(&height);
		return BLayoutUtils::ComposeSize(ExplicitMinSize(),
			BSize(_DotSize() + be_control_look->DefaultLabelSpacing()
					+ StringWidth(fText) + 1,
				ceilf(height.ascent + height.descent + height.leading)));
	}

	virtual BSize PreferredSize()
	{
		return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), MinSize());
	}

	virtual BSize MaxSize()
	{
		BSize size = MinSize();
		size.width = B_SIZE_UNLIMITED;
		return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
	}

private:
	float _DotSize()
	{
		font_height height;
		GetFontHeight(&height);
		return floorf(height.ascent * 0.7f);
	}

private:
	BString		fText;
	rgb_color	fColor;
};


// #pragma mark - InterfaceView


InterfaceView::InterfaceView()
	:
	BGroupView(B_VERTICAL),
	fPulseCount(0),
	fDNSAge(0)
{
	SetFlags(Flags() | B_PULSE_NEEDED);

	BStringView* statusLabel = new BStringView("status label",
		B_TRANSLATE("Status:"));
	statusLabel->SetAlignment(B_ALIGN_RIGHT);
	fStatusField = new StatusField();

	fNetworkMenuField = new BMenuField(B_TRANSLATE("Network:"), new BMenu(
		B_TRANSLATE("Choose automatically")));
	fNetworkMenuField->SetAlignment(B_ALIGN_RIGHT);
	fNetworkMenuField->Menu()->SetLabelFromMarked(true);

	fAddressRow = _AddRow("address", B_TRANSLATE("IP address:"));
	fIPv6Row = _AddRow("ipv6", B_TRANSLATE("IPv6 address:"));
	fRouteRow = _AddRow("route", B_TRANSLATE("Default route:"));
	fDNSRow = _AddRow("dns", B_TRANSLATE("DNS servers:"));
	fSearchDomainRow = _AddRow("search", B_TRANSLATE("Search domain:"));

	fLinkSpeedRow = _AddRow("link speed", B_TRANSLATE("Link speed:"));
	fMacAddressRow = _AddRow("mac address", B_TRANSLATE("MAC address:"));
	fMTURow = _AddRow("mtu", B_TRANSLATE("MTU:"));

	fSecurityRow = _AddRow("security", B_TRANSLATE("Security:"));
	fBSSIDRow = _AddRow("bssid", B_TRANSLATE("BSSID:"));
	fChannelRow = _AddRow("channel", B_TRANSLATE("Channel:"));
	fRSSIRow = _AddRow("rssi", B_TRANSLATE("RSSI:"));
	fTxRateRow = _AddRow("tx rate", B_TRANSLATE("Tx rate:"));
	fPhyModeRow = _AddRow("phy mode", B_TRANSLATE("PHY mode:"));

	fSentRow = _AddRow("tx", B_TRANSLATE("Sent:"));
	fReceivedRow = _AddRow("rx", B_TRANSLATE("Received:"));

	fThroughputView = new ThroughputView();

	fToggleButton = new BButton("onoff", B_TRANSLATE("Disable"),
		new BMessage(kMsgInterfaceToggle));

	fRenegotiateButton = new BButton("heal", B_TRANSLATE("Renegotiate"),
		new BMessage(kMsgInterfaceRenegotiate));
	fRenegotiateButton->SetEnabled(false);

	const Row* rows[] = {
		&fAddressRow, &fIPv6Row, &fRouteRow, &fDNSRow, &fSearchDomainRow,
		&fLinkSpeedRow, &fMacAddressRow, &fMTURow,
		&fSecurityRow, &fBSSIDRow, &fChannelRow, &fRSSIRow, &fTxRateRow,
		&fPhyModeRow,
		&fSentRow, &fReceivedRow
	};

	BGridLayout* grid;
	BLayoutBuilder::Group<>(this)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.GetLayout(&grid)
			.Add(statusLabel, 0, 0)
			.Add(fStatusField, 1, 0)
			.AddMenuField(fNetworkMenuField, 0, 1, B_ALIGN_RIGHT, 1, 2)
		.End()
		.AddStrut(be_control_look->DefaultItemSpacing() / 2)
		.Add(fThroughputView)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fToggleButton)
			.Add(fRenegotiateButton)
		.End();

	for (size_t i = 0; i < B_COUNT_OF(rows); i++) {
		grid->AddView(rows[i]->label, 0, i + 2);
		grid->AddView(rows[i]->value, 1, i + 2);
	}
	grid->SetColumnWeight(0, 0);
	grid->SetColumnWeight(1, 1);
}


InterfaceView::~InterfaceView()
{
}


void
InterfaceView::SetTo(const char* name)
{
	if (strcmp(fInterface.Name(), name) == 0)
		return;

	fInterface.SetTo(name);
	fThroughputView->Reset();
	fDNSAge = 0;
	if (Window() != NULL)
		_Update();
}


void
InterfaceView::AttachedToWindow()
{
	Window()->SetPulseRate(kPulseRate);
	_Update();
		// Populate the fields

	fToggleButton->SetTarget(this);
	fRenegotiateButton->SetTarget(this);
}


void
InterfaceView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgJoinNetwork:
		{
			const char* name;
			if (message->FindString("name", &name) == B_OK) {
				BNetworkDevice device(fInterface.Name());
				status_t status = device.JoinNetwork(name);
				if (status != B_OK) {
					BString text
						= B_TRANSLATE("Could not join wireless network:\n");
					text << strerror(status);
					(new BAlert(name, text.String(),
						B_TRANSLATE("OK")))->Go(NULL);
				}
			}
			break;
		}
		case kMsgInterfaceToggle:
		{
			// Disable/enable interface
			uint32 flags = fInterface.Flags();
			if ((flags & IFF_UP) != 0)
				flags &= ~IFF_UP;
			else
				flags |= IFF_UP;

			if (fInterface.SetFlags(flags) == B_OK)
				_Update();
			break;
		}

		case kMsgInterfaceRenegotiate:
		{
			// TODO: renegotiate addresses
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


void
InterfaceView::Pulse()
{
	// Update the wireless network menu every 5 seconds
	_Update((++fPulseCount % 5) == 0);
}


/*!	Populate fields with current settings.
*/
status_t
InterfaceView::_Update(bool updateWirelessNetworks)
{
	BNetworkDevice device(fInterface.Name());
	bool isWireless = device.IsWireless();
	bool disabled = (fInterface.Flags() & IFF_UP) == 0;

	WiFiLinkDetails link;
	BString network;
	bool associated = isWireless
		&& GetWiFiLinkDetails(fInterface.Name(), link, network) == B_OK;

	_UpdateStatus();
	_UpdateAddresses();
	_UpdateLink(associated ? &link : NULL);
	_UpdateWireless(associated ? &link : NULL);
	if (updateWirelessNetworks)
		_UpdateWirelessMenu(isWireless);
	_UpdateStats();

	fToggleButton->SetLabel(disabled
		? B_TRANSLATE("Enable") : B_TRANSLATE("Disable"));

	return B_OK;
}


void
InterfaceView::_UpdateStatus()
{
	interface_status status = GetInterfaceStatus(fInterface);
	fStatusField->SetStatus(InterfaceStatusText(status),
		InterfaceStatusColor(status));
}


void
InterfaceView::_UpdateAddresses()
{
	BString address;
	BString ipv6;
	BNetworkAddress primaryAddress;

	int32 count = fInterface.CountAddresses();
	for (int32 i = 0; i < count; i++) {
		BNetworkInterfaceAddress interfaceAddress;
		if (fInterface.GetAddressAt(i, interfaceAddress) != B_OK)
			continue;

		const BNetworkAddress& local = interfaceAddress.Address();
		if (local.IsEmpty() || local.IsWildcard())
			continue;

		BString text = local.ToString(false);
		ssize_t prefix = interfaceAddress.Mask().PrefixLength();
		if (prefix >= 0)
			text << "/" << prefix;

		if (local.Family() == AF_INET) {
			if (primaryAddress.IsEmpty())
				primaryAddress = local;
			if (!address.IsEmpty())
				address << ", ";
			address << text;
		} else if (local.Family() == AF_INET6) {
			if (!ipv6.IsEmpty())
				ipv6 << ", ";
			ipv6 << text;
		}
	}

	_SetRow(fAddressRow, address.IsEmpty() ? BString("-") : address);
	_SetRow(fIPv6Row, ipv6, !ipv6.IsEmpty());

	BNetworkAddress gateway;
	BString route;
	if (fInterface.GetDefaultGateway(AF_INET, gateway) == B_OK
		&& !gateway.IsEmpty()) {
		route = gateway.ToString(false);
		if ((fInterface.Flags() & IFF_LINK) == 0)
			route << " " << B_TRANSLATE("(no link)");
		else if (!primaryAddress.IsEmpty()
			&& _IsPrimaryInterface(primaryAddress))
			route << " " << B_TRANSLATE("(primary)");
		else
			route << " " << B_TRANSLATE("(standby)");
	} else
		route = B_TRANSLATE("None");
	_SetRow(fRouteRow, route);

	// The resolver configuration is global; reading it once in a while is
	// enough.
	if (fDNSAge-- <= 0) {
		fDNSServers = _DNSServers(fSearchDomain);
		fDNSAge = 5;
	}
	_SetRow(fDNSRow, fDNSServers.IsEmpty()
		? BString(B_TRANSLATE("None")) : fDNSServers);
	_SetRow(fSearchDomainRow, fSearchDomain, !fSearchDomain.IsEmpty());
}


void
InterfaceView::_UpdateLink(const WiFiLinkDetails* wireless)
{
	BNetworkDevice device(fInterface.Name());

	BNetworkAddress hardwareAddress;
	if (device.GetHardwareAddress(hardwareAddress) == B_OK)
		_SetRow(fMacAddressRow, hardwareAddress.ToString());
	else
		_SetRow(fMacAddressRow, "-");

	BString mtu;
	mtu << fInterface.MTU();
	_SetRow(fMTURow, mtu);

	int media = fInterface.Media();
	if ((media & IFM_ACTIVE) == 0 || (fInterface.Flags() & IFF_LINK) == 0) {
		_SetRow(fLinkSpeedRow, "-");
		return;
	}

	uint64 speed = fInterface.LinkSpeed();
	if (wireless != NULL && wireless->txKbps > 0) {
		// The rate the driver last transmitted at is the best guess
		speed = (uint64)wireless->txKbps * 1000;
	}

	// The media name without its speed, for example "1000BASE-T"
	BString detail;
	const char* mediaName = media_type_to_string(media);
	if (mediaName != NULL && strcmp(mediaName, "Auto-select") != 0) {
		detail = mediaName;
		int32 comma = detail.FindFirst(", ");
		if (comma >= 0)
			detail.Remove(0, comma + 2);
	}
	if ((media & IFM_FULL_DUPLEX) != 0) {
		if (!detail.IsEmpty())
			detail << ", ";
		detail << B_TRANSLATE("full duplex");
	} else if ((media & IFM_HALF_DUPLEX) != 0) {
		if (!detail.IsEmpty())
			detail << ", ";
		detail << B_TRANSLATE("half duplex");
	}

	BString text;
	if (speed > 0)
		text = _FormatLinkSpeed(speed);
	else
		text = B_TRANSLATE("Not reported");
	if (!detail.IsEmpty())
		text << " (" << detail << ")";
	_SetRow(fLinkSpeedRow, text);
}


void
InterfaceView::_UpdateWireless(const WiFiLinkDetails* wireless)
{
	bool associated = wireless != NULL;
	WiFiLinkDetails link;
	if (associated)
		link = *wireless;

	_SetRow(fSecurityRow, link.security,
		associated && !link.security.IsEmpty());
	_SetRow(fBSSIDRow, link.bssid, associated && !link.bssid.IsEmpty());

	BString channel;
	if (link.channel > 0) {
		channel << link.channel << " (";
		if (link.frequency >= 5925)
			channel << "6 GHz";
		else if (link.frequency >= 4900)
			channel << "5 GHz";
		else
			channel << "2.4 GHz";
		channel << ")";
	}
	_SetRow(fChannelRow, channel, associated && !channel.IsEmpty());

	BString rssi;
	if (link.dBm != 0)
		rssi << link.dBm << " dBm";
	_SetRow(fRSSIRow, rssi, associated && !rssi.IsEmpty());

	BString rate;
	if (link.txKbps > 0) {
		if (link.txKbps % 1000 == 0 || link.txKbps >= 100000)
			rate << (link.txKbps + 500) / 1000;
		else
			rate.SetToFormat("%.1f", link.txKbps / 1000.0);
		rate << " Mb/s";
		if (!link.txDetails.IsEmpty())
			rate << " (" << link.txDetails << ")";
	}
	_SetRow(fTxRateRow, rate, associated && !rate.IsEmpty());
	_SetRow(fPhyModeRow, link.phyMode, associated && !link.phyMode.IsEmpty());
}


void
InterfaceView::_UpdateWirelessMenu(bool isWireless)
{
	// TODO: move the wireless info to a separate tab. We should have a
	// BListView of available networks, rather than a menu, to make them more
	// readable and easier to browse and select.
	if (fNetworkMenuField->IsHidden(fNetworkMenuField) && isWireless)
		fNetworkMenuField->Show();
	else if (!fNetworkMenuField->IsHidden(fNetworkMenuField) && !isWireless)
		fNetworkMenuField->Hide();

	if (!isWireless)
		return;

	BNetworkDevice device(fInterface.Name());
	BMenu* menu = fNetworkMenuField->Menu();
	while (menu->CountItems() > 0)
		delete menu->RemoveItem((int32)0);

	// Group access points by network name, like the WiFi preferences
	wireless_network* networks = NULL;
	uint32 count = 0;
	device.GetNetworks(networks, count);
	if (count <= 1 && (fPulseCount % 15) == 0
		&& (fInterface.Flags() & IFF_UP) != 0) {
		// Some drivers only keep the associated access point after
		// joining. Scanning a disabled interface would turn it back on.
		device.Scan(false, false);
		delete[] networks;
		networks = NULL;
		device.GetNetworks(networks, count);
	}
	ArrayDeleter<wireless_network> networksDeleter(networks);
	BString associatedName = AssociatedWirelessNetworkName(device,
		networks, count);
	std::vector<WirelessNetworkGroup> visible = GroupWirelessNetworks(
		networks, count, associatedName.String());

	if (visible.empty()) {
		BMenuItem* item = new BMenuItem(
			B_TRANSLATE("<no wireless networks found>"), NULL);
		item->SetEnabled(false);
		menu->AddItem(item);
	} else {
		menu->AddItem(new BMenuItem(
			B_TRANSLATE("Choose automatically"), NULL));
		menu->AddSeparatorItem();
		for (size_t i = 0; i < visible.size(); i++) {
			wireless_network network = visible[i].network;
			// The menu item draws the signal as a percentage
			network.signal_strength = WirelessSignalPercent(network);

			BMessage* message = new BMessage(kMsgJoinNetwork);
			message->AddString("device", fInterface.Name());
			message->AddString("name", network.name);
			BMenuItem* item = new WirelessNetworkMenuItem(network, message);
			item->SetMarked(visible[i].connected);
			menu->AddItem(item);
		}
	}
	menu->SetTargetForItems(this);
}


void
InterfaceView::_UpdateStats()
{
	ifreq_stats stats;
	if (fInterface.GetStats(stats) != B_OK) {
		_SetRow(fSentRow, "-");
		_SetRow(fReceivedRow, "-");
		return;
	}

	char buffer[100];
	string_for_size(stats.send.bytes, buffer, sizeof(buffer));
	_SetRow(fSentRow, buffer);
	string_for_size(stats.receive.bytes, buffer, sizeof(buffer));
	_SetRow(fReceivedRow, buffer);

	fThroughputView->AddSample(stats.receive.bytes, stats.send.bytes);
}


InterfaceView::Row
InterfaceView::_AddRow(const char* name, const char* label)
{
	Row row;
	BString labelName(name);
	labelName << " label";
	row.label = new BStringView(labelName, label);
	row.label->SetAlignment(B_ALIGN_RIGHT);

	BString fieldName(name);
	fieldName << " field";
	row.value = new BStringView(fieldName, "");
	row.value->SetTruncation(B_TRUNCATE_END);

	// Keep long values (IPv6 addresses, DNS server lists) from making the
	// window grow; they are truncated instead.
	font_height height;
	be_plain_font->GetHeight(&height);
	row.value->SetExplicitMinSize(BSize(
		ceilf(height.ascent + height.descent) * 19, B_SIZE_UNSET));
	row.value->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	return row;
}


void
InterfaceView::_SetRow(const Row& row, const BString& value, bool visible)
{
	if (visible) {
		if (value != row.value->Text()) {
			row.value->SetText(value);
			row.value->SetToolTip(value.Length() > 30 ? value.String() : NULL);
		}
		if (row.label->IsHidden(row.label)) {
			row.label->Show();
			row.value->Show();
		}
	} else if (!row.label->IsHidden(row.label)) {
		row.label->Hide();
		row.value->Hide();
	}
}


/*!	Returns whether the stack sends Internet traffic from \a address, that
	is, whether its interface carries the default route in use.
*/
/*static*/ bool
InterfaceView::_IsPrimaryInterface(const BNetworkAddress& address)
{
	int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (socket < 0)
		return false;
	FileDescriptorCloser closer(socket);

	// Any address outside the local networks takes the default route.
	// Linux has no SIOCGETRT: connecting a datagram socket (which sends
	// nothing) makes the kernel pick the route and its source address.
	sockaddr_in destination;
	memset(&destination, 0, sizeof(destination));
	destination.sin_family = AF_INET;
	destination.sin_port = htons(53);
	destination.sin_addr.s_addr = inet_addr("1.1.1.1");
	if (connect(socket, (sockaddr*)&destination, sizeof(destination)) < 0)
		return false;

	sockaddr_in source;
	socklen_t sourceLength = sizeof(source);
	if (getsockname(socket, (sockaddr*)&source, &sourceLength) < 0
		|| source.sin_addr.s_addr == INADDR_ANY)
		return false;

	BNetworkAddress sourceAddress(source);
	return sourceAddress.Equals(address, false);
}


/*!	Formats a link speed like the network settings of other systems do:
	"1000 Mb/s", "2500 Mb/s", "866.7 Mb/s".
*/
/*static*/ BString
InterfaceView::_FormatLinkSpeed(uint64 bitsPerSecond)
{
	BString text;
	if (bitsPerSecond >= 1000000) {
		if (bitsPerSecond % 1000000 == 0 || bitsPerSecond >= 100000000) {
			text.SetToFormat("%" B_PRIu64 " Mb/s",
				(bitsPerSecond + 500000) / 1000000);
		} else
			text.SetToFormat("%.1f Mb/s", bitsPerSecond / 1000000.0);
	} else
		text.SetToFormat("%" B_PRIu64 " kb/s", bitsPerSecond / 1000);
	return text;
}


/*!	Returns the name servers of the system resolver as a list. They are
	set in the DNS settings; the resolver uses them for all interfaces.
*/
/*static*/ BString
InterfaceView::_DNSServers(BString& searchDomain)
{
	BString servers;
	searchDomain = "";

	struct __res_state state;
	memset(&state, 0, sizeof(struct __res_state));
	if (res_ninit(&state) != 0)
		return servers;

	for (int i = 0; i < state.nscount; i++) {
		if (!servers.IsEmpty())
			servers << ", ";
		servers << inet_ntoa(state.nsaddr_list[i].sin_addr);
	}
	if (state.dnsrch[0] != NULL)
		searchDomain = state.dnsrch[0];

	res_nclose(&state);
	return servers;
}
