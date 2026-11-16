/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include <LEPairingSession.h>

#include <LEBondStore.h>
#include <LEAttributeClient.h>
#include <LEHIDService.h>
#include <LEHIDMouseDecoder.h>
#include <LELegacyPairingCrypto.h>
#include <LELegacyPairingClient.h>
#include <LELog.h>
#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetoothserver_p.h>

#include <Autolock.h>
#include <Locker.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>

#include <deque>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>


namespace Bluetooth {

#define LOG(level, format, args...) LELog(level, "pair", format, ##args)

// How long the peer may take to answer a connection attempt. A peripheral in
// pairing mode advertises every few tens of milliseconds.
static const bigtime_t kConnectTimeout = 20000000;
static const bigtime_t kEncryptTimeout = 10000000;

struct LinkEvent {
	uint32 what;
	status_t status;
};


static void
ClearSecret(void* data, size_t length)
{
	volatile uint8* bytes = (volatile uint8*)data;
	while (length-- > 0)
		*bytes++ = 0;
}


struct ScopedBondKey : public LELegacyBondKey {
	ScopedBondKey() { memset(this, 0, sizeof(*this)); }
	~ScopedBondKey() { ClearSecret(this, sizeof(*this)); }
};


static const char*
StatusText(status_t status)
{
	// Link notices carry either a Haiku error or a raw HCI status (< 0x100).
	if (status > 0 && status < 0x100) {
		static char buffer[64];
		const char* name;
		switch (status) {
			case 0x02: name = "unknown connection"; break;
			case 0x05: name = "authentication failure"; break;
			case 0x06: name = "PIN or key missing"; break;
			case 0x08: name = "connection timeout"; break;
			case 0x0c: name = "command disallowed"; break;
			case 0x13: name = "remote user terminated connection"; break;
			case 0x16: name = "connection terminated by local host"; break;
			case 0x22: name = "LMP/LL response timeout"; break;
			case 0x3b: name = "unacceptable connection parameters"; break;
			case 0x3d: name = "MIC failure"; break;
			case 0x3e: name = "connection failed to be established"; break;
			default: name = "HCI error"; break;
		}
		snprintf(buffer, sizeof(buffer), "%s (HCI %#x)", name, (int)status);
		return buffer;
	}
	return strerror(status);
}


static const char*
EventName(uint32 what)
{
	switch (what) {
		case BT_MSG_LE_CONNECTING: return "connecting";
		case BT_MSG_LE_CONNECTED: return "connected";
		case BT_MSG_LE_CONNECT_FAILED: return "connect failed";
		case BT_MSG_LE_DISCONNECTED: return "disconnected";
		case BT_MSG_LE_ENCRYPTED: return "encrypted";
		case BT_MSG_LE_ENCRYPTION_FAILED: return "encryption failed";
		default: return "?";
	}
}


static void
SetDetail(LEPairingResult& result, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(result.detail, sizeof(result.detail), format, args);
	va_end(args);
}


class PairingListener : public BLooper {
public:
	PairingListener()
		:
		BLooper("LE pairing listener"),
		fLock("LE pairing events"),
		fSemaphore(create_sem(0, "LE pairing events")),
		fDisconnected(false),
		fDisconnectStatus(B_OK)
	{
	}

	~PairingListener()
	{
		if (fSemaphore >= 0)
			delete_sem(fSemaphore);
	}

	bool IsReady() const { return fSemaphore >= 0; }

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case BT_MSG_LE_CONNECTING:
			case BT_MSG_LE_CONNECTED:
			case BT_MSG_LE_CONNECT_FAILED:
			case BT_MSG_LE_DISCONNECTED:
			case BT_MSG_LE_ENCRYPTED:
			case BT_MSG_LE_ENCRYPTION_FAILED:
			{
				LinkEvent event = { message->what, B_ERROR };
				message->FindInt32("status", &event.status);
				LOG(event.what == BT_MSG_LE_CONNECTING ? LE_LOG_DEBUG
						: LE_LOG_INFO, "link event: %s (%s)",
					EventName(event.what), StatusText(event.status));
				BAutolock lock(&fLock);
				if (lock.IsLocked()) {
					if (event.what == BT_MSG_LE_DISCONNECTED) {
						fDisconnected = true;
						fDisconnectStatus = event.status;
					}
					fEvents.push_back(event);
					release_sem(fSemaphore);
				}
				break;
			}
			default:
				BLooper::MessageReceived(message);
		}
	}

	status_t WaitFor(uint32 success, LinkEvent& event, bigtime_t timeout)
	{
		bigtime_t deadline = system_time() + timeout;
		for (;;) {
			status_t status = acquire_sem_etc(fSemaphore, 1,
				B_ABSOLUTE_TIMEOUT, deadline);
			if (status != B_OK)
				return status;
			BAutolock lock(&fLock);
			if (!lock.IsLocked() || fEvents.empty())
				return B_ERROR;
			event = fEvents.front();
			fEvents.pop_front();
			if (event.what == success)
				return event.status;
			if (event.what == BT_MSG_LE_CONNECT_FAILED
				|| event.what == BT_MSG_LE_DISCONNECTED
				|| event.what == BT_MSG_LE_ENCRYPTION_FAILED)
				return event.status == B_OK ? B_ERROR : event.status;
		}
	}

	bool Disconnected(status_t* reason)
	{
		BAutolock lock(&fLock);
		if (reason != NULL)
			*reason = fDisconnectStatus;
		return fDisconnected;
	}

private:
	BLocker fLock;
	sem_id fSemaphore;
	std::deque<LinkEvent> fEvents;
	bool fDisconnected;
	status_t fDisconnectStatus;
};


static status_t
SendRequest(BMessenger& server, BMessage& request)
{
	BMessage reply;
	status_t status = server.SendMessage(&request, &reply, 5000000, 5000000);
	if (status != B_OK) {
		LOG(LE_LOG_ERROR, "Bluetooth server did not answer request %.4s: %s",
			(const char*)&request.what, strerror(status));
		return status;
	}
	int32 operationStatus;
	return reply.FindInt32("status", &operationStatus) == B_OK
		? operationStatus : B_BAD_DATA;
}


static void
SetStage(LEPairingResult& result, LEPairingStage stage,
	const BMessenger* progress, const char* detail = NULL)
{
	result.stage = stage;
	if (progress != NULL && progress->IsValid()) {
		BMessage message(LE_PAIRING_PROGRESS_MESSAGE);
		message.AddInt32("stage", stage);
		if (detail != NULL)
			message.AddString("detail", detail);
		progress->SendMessage(&message);
	}
}


static status_t
ControlLink(BMessenger& server, uint32 what, int32 hciID,
	const BMessenger* listener)
{
	BMessage request(what);
	request.AddInt32("hci_id", hciID);
	if (listener != NULL)
		request.AddMessenger("listener", *listener);
	return SendRequest(server, request);
}


// Asks the server for a connection; retries while another client (for
// example the input add-on reconnecting a mouse) holds the single LE link.
static status_t
RequestConnection(BMessenger& server, int32 hciID, PairingListener* listener,
	const uint8 address[6], uint8 addressType, bigtime_t busyTimeout,
	const uint16* parameters = NULL)
{
	bigtime_t deadline = system_time() + busyTimeout;
	for (;;) {
		BMessage request(BT_MSG_LE_CONNECT);
		request.AddInt32("hci_id", hciID);
		request.AddMessenger("listener", BMessenger(listener));
		request.AddData("address", B_RAW_TYPE, address, 6);
		request.AddUInt8("address_type", addressType);
		if (parameters != NULL) {
			request.AddUInt16("interval_min", parameters[0]);
			request.AddUInt16("interval_max", parameters[1]);
			request.AddUInt16("latency", parameters[2]);
			request.AddUInt16("timeout", parameters[3]);
		}
		status_t status = SendRequest(server, request);
		if (status != B_BUSY || system_time() > deadline)
			return status;
		LOG(LE_LOG_INFO, "LE link busy (another client); retrying");
		snooze(500000);
	}
}


class BondedPeerScanner : public BLooper {
public:
	BondedPeerScanner(const uint8 peerAddress[6], uint8 peerType,
		const LELegacyBondKey& bond)
		:
		BLooper("LE bonded peer scan"),
		fLock("LE bonded peer scan"),
		fSemaphore(create_sem(0, "LE bonded peer scan")),
		fBond(bond),
		fOriginalType(peerType),
		fFound(false),
		fStopped(false),
		fError(false)
	{
		memcpy(fOriginal, peerAddress, 6);
		memset(fAddress, 0, sizeof(fAddress));
	}

	~BondedPeerScanner() override
	{
		if (fSemaphore >= 0)
			delete_sem(fSemaphore);
	}

	bool IsReady() const { return fSemaphore >= 0; }

	void MessageReceived(BMessage* message) override
	{
		if (message->what == BT_MSG_LE_ADVERTISEMENT) {
			uint8 eventType, addressType;
			const uint8* address;
			ssize_t length;
			// Only connectable advertisements (ADV_IND, ADV_DIRECT_IND).
			if (message->FindUInt8("event_type", &eventType) != B_OK
				|| eventType > 1
				|| message->FindUInt8("address_type", &addressType) != B_OK
				|| addressType > 1
				|| message->FindData("address", B_RAW_TYPE,
					(const void**)&address, &length) != B_OK || length != 6)
				return;
			bool matches = (addressType == fOriginalType
					&& memcmp(address, fOriginal, 6) == 0)
				|| (fBond.hasIdentity
					&& ((addressType == fBond.identityAddressType
							&& memcmp(address, fBond.identityAddress, 6) == 0)
						|| (addressType == 1 && (address[5] & 0xc0) == 0x40
							&& LEResolvePrivateAddress(
								fBond.identityResolvingKey, address))));
			if (!matches)
				return;
			BAutolock lock(&fLock);
			if (lock.IsLocked() && !fFound) {
				memcpy(fAddress, address, 6);
				fAddressType = addressType;
				fFound = true;
				char text[18];
				LOG(LE_LOG_INFO, "bonded peer advertising as %s (type %u, %s)",
					LEAddressString(address, text), addressType,
					eventType == 1 ? "directed" : "undirected");
				release_sem(fSemaphore);
			}
		} else if (message->what == BT_MSG_LE_SCAN_STOPPED
			|| message->what == BT_MSG_LE_SCAN_ERROR) {
			BAutolock lock(&fLock);
			if (lock.IsLocked()) {
				fStopped = true;
				fError = message->what == BT_MSG_LE_SCAN_ERROR;
				release_sem(fSemaphore);
			}
		} else
			BLooper::MessageReceived(message);
	}

	status_t WaitForAddress(uint8 address[6], uint8& type, bigtime_t timeout)
	{
		bigtime_t deadline = system_time() + timeout;
		for (;;) {
			{
				BAutolock lock(&fLock);
				if (!lock.IsLocked())
					return B_ERROR;
				if (fFound) {
					memcpy(address, fAddress, 6);
					type = fAddressType;
					return B_OK;
				}
				if (fStopped)
					return fError ? B_ERROR : B_ENTRY_NOT_FOUND;
			}
			status_t status = acquire_sem_etc(fSemaphore, 1,
				B_ABSOLUTE_TIMEOUT, deadline);
			if (status != B_OK)
				return status;
		}
	}

private:
	BLocker fLock;
	sem_id fSemaphore;
	const LELegacyBondKey& fBond;
	uint8 fOriginal[6];
	uint8 fOriginalType;
	uint8 fAddress[6];
	uint8 fAddressType;
	bool fFound;
	bool fStopped;
	bool fError;
};


static status_t
FindBondedPeer(BMessenger& server, int32 hciID,
	const uint8 peerAddress[6], uint8 peerType,
	const LELegacyBondKey& bond, uint8 currentAddress[6], uint8& currentType,
	bigtime_t timeout)
{
	BondedPeerScanner* scanner = new BondedPeerScanner(peerAddress, peerType,
		bond);
	if (scanner == NULL || !scanner->IsReady()
		|| scanner->Run() < B_OK) {
		delete scanner;
		return B_NO_MEMORY;
	}
	BMessenger listener(scanner);
	BMessage request(BT_MSG_LE_SCAN_START);
	request.AddInt32("hci_id", hciID);
	request.AddMessenger("listener", listener);
	status_t status = SendRequest(server, request);
	if (status == B_OK) {
		status = scanner->WaitForAddress(currentAddress, currentType, timeout);
		// Stop only this client's interest; other scanners keep running.
		ControlLink(server, BT_MSG_LE_SCAN_STOP, hciID, &listener);
	} else
		LOG(LE_LOG_ERROR, "could not start LE scan: %s", strerror(status));
	if (scanner->Lock())
		scanner->Quit();
	return status;
}


struct LEEncryptedLink::Impl {
	Impl()
		:
		listener(NULL),
		hciID(-1),
		connected(false),
		hasParameters(false)
	{
		memset(connectionAddress, 0, sizeof(connectionAddress));
		memset(parameters, 0, sizeof(parameters));
	}

	BMessenger server;
	PairingListener* listener;
	int32 hciID;
	bool connected;
	uint8 connectionAddress[6];
	bool hasParameters;
	uint16 parameters[4];
};


LEEncryptedLink::LEEncryptedLink()
	:
	fImpl(new Impl())
{
}


LEEncryptedLink::~LEEncryptedLink()
{
	Disconnect();
	delete fImpl;
}


status_t
LEEncryptedLink::ConnectBonded(int32 hciID, const uint8 localAddress[6],
	const uint8 peerAddress[6], uint8 peerAddressType, bigtime_t scanTimeout)
{
	if (fImpl == NULL)
		return B_NO_MEMORY;
	if (hciID < 0 || localAddress == NULL || peerAddress == NULL
		|| peerAddressType > 1)
		return B_BAD_VALUE;
	if (fImpl->connected || fImpl->listener != NULL)
		return B_BUSY;
	char directory[1024];
	status_t status = DefaultLEBondDirectory(directory, sizeof(directory));
	if (status != B_OK)
		return status;
	ScopedBondKey bond;
	status = LoadLEBond(directory, localAddress, 0, peerAddress,
		peerAddressType, bond);
	if (status != B_OK) {
		LOG(LE_LOG_ERROR, "no usable bond for the saved device: %s",
			strerror(status));
		return status;
	}
	uint8 currentAddress[6];
	memcpy(currentAddress, peerAddress, 6);
	uint8 currentType = peerAddressType;

	fImpl->server = BMessenger(BLUETOOTH_SIGNATURE);
	if (!fImpl->server.IsValid())
		return B_NO_INIT;

	// Connect only once the peer is seen advertising: a pending connection
	// attempt would otherwise hold the controller's only initiator.
	status = FindBondedPeer(fImpl->server, hciID, peerAddress,
		peerAddressType, bond, currentAddress, currentType, scanTimeout);
	if (status != B_OK) {
		LOG(status == B_TIMED_OUT ? LE_LOG_DEBUG : LE_LOG_INFO,
			"bonded peer not found: %s", strerror(status));
		return status;
	}
	fImpl->listener = new PairingListener();
	if (fImpl->listener == NULL || !fImpl->listener->IsReady()
		|| fImpl->listener->Run() < B_OK) {
		delete fImpl->listener;
		fImpl->listener = NULL;
		return B_NO_MEMORY;
	}
	BMessenger listener(fImpl->listener);
	fImpl->hciID = hciID;
	status = RequestConnection(fImpl->server, hciID, fImpl->listener,
		currentAddress, currentType, 10000000,
		fImpl->hasParameters ? fImpl->parameters : NULL);
	if (status != B_OK) {
		LOG(LE_LOG_ERROR, "LE connect request refused: %s", StatusText(status));
		Disconnect();
		return status;
	}
	LinkEvent event;
	status = fImpl->listener->WaitFor(BT_MSG_LE_CONNECTED, event,
		kConnectTimeout);
	if (status != B_OK) {
		LOG(LE_LOG_ERROR, "reconnecting the bonded peer failed: %s",
			StatusText(status));
		ControlLink(fImpl->server, BT_MSG_LE_CONNECT_CANCEL, hciID, &listener);
		Disconnect();
		return status;
	}
	fImpl->connected = true;
	memcpy(fImpl->connectionAddress, currentAddress, 6);
	BMessage encrypt(BT_MSG_LE_START_ENCRYPTION);
	encrypt.AddInt32("hci_id", hciID);
	encrypt.AddData("long_term_key", B_RAW_TYPE, bond.longTermKey, 16);
	encrypt.AddData("random_number", B_RAW_TYPE, bond.randomNumber, 8);
	encrypt.AddUInt16("encrypted_diversifier", bond.encryptedDiversifier);
	status = SendRequest(fImpl->server, encrypt);
	if (status == B_OK) {
		status = fImpl->listener->WaitFor(BT_MSG_LE_ENCRYPTED, event,
			kEncryptTimeout);
	}
	if (status != B_OK) {
		LOG(LE_LOG_ERROR, "encrypting with the stored key failed: %s%s",
			StatusText(status), status == 0x06 ? "; the device has probably "
				"forgotten this computer and must be paired again" : "");
		Disconnect();
	} else
		LOG(LE_LOG_INFO, "bonded peer connected and encrypted");
	return status;
}


void
LEEncryptedLink::SetConnectionParameters(uint16 intervalMin,
	uint16 intervalMax, uint16 latency, uint16 timeout)
{
	if (fImpl == NULL)
		return;
	fImpl->hasParameters = true;
	fImpl->parameters[0] = intervalMin;
	fImpl->parameters[1] = intervalMax;
	fImpl->parameters[2] = latency;
	fImpl->parameters[3] = timeout;
}


status_t
LEEncryptedLink::KeepInterval(uint16 intervalMin, uint16 intervalMax)
{
	if (fImpl == NULL || !fImpl->connected)
		return B_NO_INIT;
	BMessage request(BT_MSG_LE_UPDATE_CONNECTION);
	request.AddInt32("hci_id", fImpl->hciID);
	request.AddUInt16("interval_min", intervalMin);
	request.AddUInt16("interval_max", intervalMax);
	request.AddBool("keep_interval", true);
	return SendRequest(fImpl->server, request);
}


void
LEEncryptedLink::Disconnect()
{
	if (fImpl == NULL)
		return;
	BMessenger listener(fImpl->listener);
	if (fImpl->connected) {
		ControlLink(fImpl->server, BT_MSG_LE_DISCONNECT, fImpl->hciID,
			&listener);
	}
	fImpl->connected = false;
	fImpl->hciID = -1;
	memset(fImpl->connectionAddress, 0, sizeof(fImpl->connectionAddress));
	if (fImpl->listener != NULL) {
		if (fImpl->listener->Lock())
			fImpl->listener->Quit();
		fImpl->listener = NULL;
	}
}


bool
LEEncryptedLink::IsConnected() const
{
	if (fImpl == NULL || !fImpl->connected)
		return false;
	return fImpl->listener == NULL || !fImpl->listener->Disconnected(NULL);
}


void
LEEncryptedLink::ConnectionAddress(uint8 address[6]) const
{
	if (address == NULL)
		return;
	if (fImpl != NULL && fImpl->connected)
		memcpy(address, fImpl->connectionAddress, 6);
	else
		memset(address, 0, 6);
}


static status_t
OpenSMPChannel(const uint8 peerAddress[6], int& smpSocket)
{
	// TODO(linux backend): bluetoothd runs SMP and stores the keys itself;
	// LE pairing goes through org.bluez.Device1.Pair() with the agent that
	// BlueZBackend registers. This SMP channel uses Haiku's L2CAP socket
	// address and has no Linux counterpart.
	smpSocket = -1;
	return B_NOT_SUPPORTED;

	smpSocket = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
	if (smpSocket < 0)
		return errno;
	timeval sendTimeout = { 10, 0 };
	if (setsockopt(smpSocket, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout,
			sizeof(sendTimeout)) != 0)
		return errno;
	sockaddr_l2cap peer = {};
	peer.l2cap_len = sizeof(peer);
	peer.l2cap_family = AF_BLUETOOTH;
	peer.l2cap_psm = B_L2CAP_LE_SMP_CID;
	memcpy(peer.l2cap_bdaddr.b, peerAddress, 6);
	// The kernel registers the link when it sees the controller's event,
	// which can trail the server's notice slightly.
	status_t status = B_ERROR;
	for (int attempt = 0; attempt < 10; attempt++) {
		if (connect(smpSocket, (const sockaddr*)&peer, sizeof(peer)) == 0)
			return B_OK;
		status = errno;
		if (status != ENETUNREACH && status != EHOSTUNREACH)
			break;
		snooze(100000);
	}
	return status;
}


static void
ProbeHIDMouse(LEPairingResult& result, const uint8 localAddress[6],
	const uint8 peerAddress[6], uint8 peerAddressType)
{
	bdaddr_t peer = {};
	memcpy(peer.b, peerAddress, 6);
	LEAttributeClient client;
	result.hidStatus = client.Connect(peer);
	if (result.hidStatus != B_OK) {
		SetDetail(result, "Paired, but the attribute channel could not be "
			"opened: %s.", strerror(result.hidStatus));
		return;
	}
	LEHIDService service;
	result.hidStatus = service.Discover(client);
	if (result.hidStatus != B_OK) {
		SetDetail(result, "Paired, but no HID service was found (%s, ATT "
			"error %#x).", strerror(result.hidStatus), client.LastATTError());
		return;
	}
	LOG(LE_LOG_INFO, "HID service: %zu-byte report map, %zu input report(s)",
		service.ReportMap().size(), service.InputReports().size());
	// Game pads (an Xbox Series controller, say) are registered like mice:
	// the input server add-on that connects them hands their reports to the
	// bluetooth_hid driver, which publishes a joystick device.
	const bool gamepad = LEHIDReportMapHasGamepad(service.ReportMap().data(),
		service.ReportMap().size());
	LEHIDMouseDecoder decoder;
	result.hidStatus = decoder.Init(service.ReportMap().data(),
		service.ReportMap().size());
	if (result.hidStatus != B_OK && !gamepad) {
		SetDetail(result, "Paired, but the HID report map could not be "
			"parsed (%s).", strerror(result.hidStatus));
		return;
	}
	bool hasMouseInput = false;
	for (const LEHIDInputReport& report : service.InputReports()) {
		bool mouse = decoder.SupportsReport(report.reportID);
		LOG(LE_LOG_INFO, "input report %u (handle %#x): %s", report.reportID,
			report.valueHandle, mouse ? "pointer" : "not a pointer report");
		hasMouseInput |= mouse;
	}
	if (gamepad)
		LOG(LE_LOG_INFO, "the report map has a game pad");
	if (!hasMouseInput && !gamepad) {
		result.hidStatus = B_ENTRY_NOT_FOUND;
		SetDetail(result, "Paired, but the device has no mouse or game pad "
			"input report.");
		return;
	}
	char directory[1024];
	result.hidStatus = DefaultLEBondDirectory(directory, sizeof(directory));
	if (result.hidStatus == B_OK) {
		result.hidStatus = SaveLEHIDMouse(directory, localAddress, peerAddress,
			peerAddressType);
	}
	result.hidMouseReady = result.hidStatus == B_OK;
	if (!result.hidMouseReady) {
		SetDetail(result, "Paired, but the mouse could not be registered for "
			"input (%s).", strerror(result.hidStatus));
	}
}


LEPairingResult
PairLEDevice(int32 hciID, const uint8 localAddress[6],
	const uint8 peerAddress[6], uint8 peerAddressType,
	const BMessenger* progress)
{
	LEPairingResult result = { B_BAD_VALUE, LE_PAIRING_CONNECT, false, false,
		false, false, B_NO_INIT, "" };
	if (hciID < 0 || localAddress == NULL || peerAddress == NULL
		|| peerAddressType > 1) {
		SetDetail(result, "Invalid pairing request.");
		return result;
	}

	char peerText[18], localText[18];
	LOG(LE_LOG_INFO, "=== pairing %s (address type %u) from adapter %" B_PRId32
		" (%s) ===", LEAddressString(peerAddress, peerText), peerAddressType,
		hciID, LEAddressString(localAddress, localText));

	BMessenger server(BLUETOOTH_SIGNATURE);
	if (!server.IsValid()) {
		result.status = B_NO_INIT;
		SetDetail(result, "The Bluetooth service is not running.");
		LOG(LE_LOG_ERROR, "%s", result.detail);
		return result;
	}
	PairingListener* listener = new PairingListener();
	if (!listener->IsReady() || listener->Run() < B_OK) {
		delete listener;
		result.status = B_NO_MEMORY;
		SetDetail(result, "Out of memory.");
		return result;
	}
	BMessenger listenerMessenger(listener);
	SetStage(result, LE_PAIRING_CONNECT, progress,
		"Waiting for the device to accept the connection");

	int smpSocket = -1;
	bool connected = false;
	char directory[1024];
	ScopedBondKey bond;
	uint8 distribution = 0;
	uint8 keySize = 0;
	LinkEvent event;
	BMessage encrypt(BT_MSG_LE_START_ENCRYPTION);
	uint8 shortTermKey[16] = {};
	bigtime_t started = system_time();

	result.status = RequestConnection(server, hciID, listener, peerAddress,
		peerAddressType, 15000000);
	if (result.status != B_OK) {
		SetDetail(result, "The Bluetooth service refused the connection: %s.",
			StatusText(result.status));
		goto done;
	}
	result.status = listener->WaitFor(BT_MSG_LE_CONNECTED, event,
		kConnectTimeout);
	if (result.status != B_OK) {
		if (result.status == B_TIMED_OUT) {
			SetDetail(result, "The device did not accept a connection within "
				"%d seconds. Make sure it is in pairing mode and close by.",
				(int)(kConnectTimeout / 1000000));
		} else
			SetDetail(result, "Connecting failed: %s.",
				StatusText(result.status));
		ControlLink(server, BT_MSG_LE_CONNECT_CANCEL, hciID,
			&listenerMessenger);
		goto done;
	}
	connected = true;
	LOG(LE_LOG_INFO, "connected after %.1f s",
		(system_time() - started) / 1000000.0);

	result.status = DefaultLEBondDirectory(directory, sizeof(directory));
	if (result.status != B_OK) {
		SetDetail(result, "No settings directory for bonds: %s.",
			strerror(result.status));
		goto done;
	}

	SetStage(result, LE_PAIRING_SMP_SOCKET, progress,
		"Opening the security channel");
	result.status = OpenSMPChannel(peerAddress, smpSocket);
	if (result.status != B_OK) {
		status_t reason;
		if (listener->Disconnected(&reason)) {
			SetDetail(result, "The device dropped the connection before "
				"pairing started: %s.", StatusText(reason));
		} else {
			SetDetail(result, "Could not open the security channel: %s. The "
				"kernel log (syslog, \"l2cap-le\") has details.",
				strerror(result.status));
		}
		goto done;
	}

	SetStage(result, LE_PAIRING_SMP_REQUEST, progress,
		"Exchanging pairing features");
	result.status = LELegacyPairJustWorks(smpSocket, localAddress, 0,
		peerAddress, peerAddressType, shortTermKey, &distribution,
		&keySize, progress, result.detail, sizeof(result.detail));
	if (result.status != B_OK) {
		status_t reason;
		if (listener->Disconnected(&reason)) {
			SetDetail(result, "The device disconnected during pairing (%s).",
				StatusText(reason));
		}
		goto done;
	}

	SetStage(result, LE_PAIRING_ENCRYPT, progress, "Encrypting the link");
	encrypt.AddInt32("hci_id", hciID);
	encrypt.AddData("short_term_key", B_RAW_TYPE, shortTermKey, 16);
	ClearSecret(shortTermKey, sizeof(shortTermKey));
	result.status = SendRequest(server, encrypt);
	if (result.status == B_OK) {
		result.status = listener->WaitFor(BT_MSG_LE_ENCRYPTED, event,
			kEncryptTimeout);
	}
	if (result.status != B_OK) {
		SetDetail(result, "Encrypting the link failed: %s.",
			StatusText(result.status));
		goto done;
	}
	result.encrypted = true;

	SetStage(result, LE_PAIRING_KEYS, progress,
		"Receiving the device's keys");
	result.status = LELegacyReceiveResponderKeys(smpSocket, distribution,
		keySize, bond, result.detail, sizeof(result.detail));
	if (result.status != B_OK)
		goto done;
	if (!bond.hasLongTermKey) {
		// Paired for this connection only; the device cannot reconnect.
		LOG(LE_LOG_ERROR, "device distributed no long-term key; it will not "
			"be able to reconnect");
	} else {
		SetStage(result, LE_PAIRING_SAVE, progress, "Saving the bond");
		result.status = SaveLEBond(directory, localAddress, 0, peerAddress,
			peerAddressType, bond);
		if (result.status != B_OK) {
			SetDetail(result, "Could not save the bond: %s.",
				strerror(result.status));
			goto done;
		}
		result.bonded = true;
		LOG(LE_LOG_INFO, "bond saved");
	}

	if (result.bonded) {
		SetStage(result, LE_PAIRING_HID, progress,
			"Reading the device's input reports");
		ProbeHIDMouse(result, localAddress, peerAddress, peerAddressType);
		LOG(result.hidMouseReady ? LE_LOG_INFO : LE_LOG_ERROR,
			"HID mouse %s: %s", result.hidMouseReady ? "ready" : "not ready",
			result.hidMouseReady ? "input server will connect it"
				: result.detail);
	}
	SetStage(result, LE_PAIRING_COMPLETE, progress);
	result.status = B_OK;

done:
	{
		LEPairingStage outcomeStage = result.stage;
		SetStage(result, LE_PAIRING_CLEANUP, progress);
		if (smpSocket >= 0)
			close(smpSocket);
		if (connected && !listener->Disconnected(NULL)) {
			ControlLink(server, BT_MSG_LE_DISCONNECT, hciID,
				&listenerMessenger);
			// Give the link a moment to go down so the input add-on can
			// take over without a busy link.
			LinkEvent ignored;
			listener->WaitFor(BT_MSG_LE_DISCONNECTED, ignored, 3000000);
		}
		if (listener->Lock())
			listener->Quit();
		result.stage = outcomeStage;
		ClearSecret(shortTermKey, sizeof(shortTermKey));
		if (result.status != B_OK && result.detail[0] == '\0')
			SetDetail(result, "%s", StatusText(result.status));
		LOG(result.status == B_OK ? LE_LOG_INFO : LE_LOG_ERROR,
			"=== pairing %s %s after %.1f s%s%s ===", peerText,
			result.status == B_OK ? "succeeded" : "failed",
			(system_time() - started) / 1000000.0,
			result.detail[0] != '\0' ? ": " : "", result.detail);
	}
	return result;
}

} // namespace Bluetooth
