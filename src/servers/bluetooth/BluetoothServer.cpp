/*
 * Copyright 2007-2009 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>

#include <Entry.h>
#include <Directory.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>
#include <Window.h>

#include <TypeConstants.h>
#include <syslog.h>

#include <bluetoothserver_p.h>
#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetooth/bluetooth.h>

#include <LELog.h>

#include "BluetoothServer.h"
#include "Debug.h"
#include "LocalDeviceImpl.h"
#include "SDPServer.h"
#include "SDPClient.h"


status_t
DispatchEvent(struct hci_event_header* header, int32 code, size_t size)
{
	// we only handle events
	if (GET_PORTCODE_TYPE(code)!= BT_EVENT) {
		TRACE_BT("BluetoothServer: Wrong type frame code\n");
		return B_OK;
	}

	// fetch the LocalDevice who belongs this event
	LocalDeviceImpl* lDeviceImplementation = ((BluetoothServer*)be_app)->
		LocateLocalDeviceImpl(GET_PORTCODE_HID(code));

	if (lDeviceImplementation == NULL) {
		TRACE_BT("BluetoothServer: LocalDevice could not be fetched\n");
		return B_OK;
	}

	lDeviceImplementation->HandleEvent(header);

	return B_OK;
}


// Where the time goes between the server starting and the radio answering:
// every step of the way is logged with the time since boot, since the radio
// has been seen coming up minutes after the rest of the system.
void
LogStartup(const char* format, ...)
{
	char message[256];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	Bluetooth::LELog(Bluetooth::LE_LOG_INFO, "server", "%s (%" B_PRId64
		" ms after boot)", message, system_time() / 1000);
}


BluetoothServer::BluetoothServer()
	:
	BApplication(BLUETOOTH_SIGNATURE)
{
	LogStartup("application started");
	fDeviceManager = new DeviceManager();
	fLocalDevicesList.MakeEmpty();
	fWatchersList.MakeEmpty();

	fEventListener2 = new BluetoothPortListener(BT_USERLAND_PORT_NAME,
		(BluetoothPortListener::port_listener_func)&DispatchEvent);
	fSDPServer = new SDPServer();
	fHIDHost = new HIDHost();
}


// The SDP records of a device, read on a thread of their own (see
// DiscoverServices()); internal to the server.
static const uint32 kMsgServicesDiscovered = 'btSD';
// A Classic link to authenticate if it is still not encrypted by then.
static const uint32 kMsgCheckLinkSecurity = 'btCL';
static const bigtime_t kLinkSecurityDelay = 2000000;


bool BluetoothServer::QuitRequested(void)
{
#ifdef __VOS__
	fAgent.Stop();
#endif
	LocalDeviceImpl* lDeviceImpl = NULL;
	while ((lDeviceImpl = (LocalDeviceImpl*)
		fLocalDevicesList.RemoveItemAt(0)) != NULL)
		delete lDeviceImpl;

	fSDPServer->Stop();
	delete fSDPServer;

	fHIDHost->Stop();
	delete fHIDHost;
	fHIDHost = NULL;

	delete fEventListener2;
	TRACE_BT("Shutting down bluetooth_server.\n");

	return BApplication::QuitRequested();
}


void BluetoothServer::ArgvReceived(int32 argc, char **argv)
{
	if (argc > 1) {
		if (strcmp(argv[1], "--finish") == 0)
			PostMessage(B_QUIT_REQUESTED);
	}
}


void BluetoothServer::ReadyToRun(void)
{
#ifdef __VOS__
	// On Linux BlueZ owns the radios, the SDP server and the HID host
	// (bluetoothd, its input plugin and uhid), and the Bluetooth kit talks
	// to it directly (BlueZBackend). The server's part is BlueZ's pairing
	// agent: the user enters and confirms codes in air/OS's windows.
	// TODO(linux backend): the HCI device handling, SDP server and HID host
	// below drive Haiku's Bluetooth stack (/dev/bluetooth, HCI ports) and
	// are not started; A2DP routing is PipeWire's (its BlueZ plugin).
	LogStartup("pairing agent for BlueZ");
	if (fAgent.Start() != B_OK)
		LogStartup("the pairing agent did not start");
	return;
#endif
	LogStartup("looking for radios");
	// The device manager's looper has to run for the node monitor to tell
	// it about radios that appear later (a controller whose firmware is
	// loaded while the server starts, a dongle plugged in); without it only
	// what is there at this point was ever found.
	fDeviceManager->LoadState();
	fDeviceManager->StartMonitoringDevice("bluetooth/h2");
	fDeviceManager->StartMonitoringDevice("bluetooth/h3");
	fDeviceManager->StartMonitoringDevice("bluetooth/h4");
	fDeviceManager->StartMonitoringDevice("bluetooth/h5");
	LogStartup("watching for radios");

	if (fEventListener2->Launch() != B_OK)
		TRACE_BT("General: Bluetooth event listener failed\n");
	else
		TRACE_BT("General: Bluetooth event listener Ready\n");

	status_t status = fSDPServer->Start();
	if (status != B_OK)
		TRACE_BT("BluetoothServer: Failed launching the SDP server thread\n");

	status = fHIDHost->Start();
	if (status != B_OK) {
		Bluetooth::LELog(Bluetooth::LE_LOG_ERROR, "hid",
			"the HID host did not start: %s", strerror(status));
	}

	// Drives the HCI command queue and LE watchdogs.
	SetPulseRate(500000);
}


void BluetoothServer::Pulse(void)
{
	for (int32 index = 0; index < fLocalDevicesList.CountItems(); index++) {
		LocalDeviceImpl* device
			= (LocalDeviceImpl*)fLocalDevicesList.ItemAt(index);
		if (device != NULL)
			device->Pulse();
	}
}


void BluetoothServer::AppActivated(bool act)
{
	printf("Activated %d\n",act);
}


void BluetoothServer::MessageReceived(BMessage* message)
{
	BMessage reply;
	status_t status = B_WOULD_BLOCK; // mark somehow to do not reply anything

	switch (message->what) {
		case BT_MSG_ADD_DEVICE:
		{
			BString str;
			message->FindString("name", &str);

			TRACE_BT("BluetoothServer: Requested LocalDevice %s\n", str.String());

			BPath path(str.String());

			LogStartup("opening %s", str.String());
			LocalDeviceImpl* lDeviceImpl
				= LocalDeviceImpl::CreateTransportAccessor(&path);
			if (lDeviceImpl == NULL) {
				LogStartup("%s could not be opened", str.String());
				break;
			}
			LogStartup("%s open as hci %" B_PRId32, str.String(),
				lDeviceImpl->GetID());

			if (lDeviceImpl->GetID() >= 0) {
				fLocalDevicesList.AddItem(lDeviceImpl);
				fDevicePaths.RemoveName(str.String());
				fDevicePaths.AddInt32(str.String(), lDeviceImpl->GetID());

				TRACE_BT("LocalDevice %s id=%" B_PRId32 " added\n", str.String(),
					lDeviceImpl->GetID());
			} else {
				TRACE_BT("BluetoothServer: Adding LocalDevice hci id invalid\n");
			}

			status = B_WOULD_BLOCK;
			/* TODO: This should be by user request only! */
			if (lDeviceImpl->Launch() == B_OK)
				lDeviceImpl->StartPairingSetup();
		}
		break;

		case BT_MSG_REMOVE_DEVICE:
		{
			// by its hci_id, or by the path it was opened from when its node
			// went away (a radio unplugged, or restarted by its driver)
			LocalDeviceImpl* lDeviceImpl = NULL;
			const char* path;
			int32 id;
			if (message->FindString("name", &path) == B_OK) {
				if (fDevicePaths.FindInt32(path, &id) == B_OK) {
					fDevicePaths.RemoveName(path);
					lDeviceImpl = LocateLocalDeviceImpl(id);
					LogStartup("%s went away (hci %" B_PRId32 ")", path, id);
				}
			} else
				lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl != NULL) {
				fLocalDevicesList.RemoveItem(lDeviceImpl);
				delete lDeviceImpl;
			}
		}
		break;

		case BT_MSG_COUNT_LOCAL_DEVICES:
			status = HandleLocalDevicesCount(message, &reply);
			break;

		case BT_MSG_ACQUIRE_LOCAL_DEVICE:
			status = HandleAcquireLocalDevice(message, &reply);
			break;

		case BT_MSG_HANDLE_SIMPLE_REQUEST:
			status = HandleSimpleRequest(message, &reply);
			break;

		case BT_MSG_LE_SCAN_START:
		{
			LocalDeviceImpl* device = LocateDelegateFromMessage(message);
			BMessenger listener;
			status = device != NULL
				&& message->FindMessenger("listener", &listener) == B_OK
				? device->StartLEScan(listener) : B_BAD_VALUE;
			break;
		}

		case BT_MSG_LE_SCAN_STOP:
		{
			// With a "listener", only that client's scan interest ends;
			// without one every listener is stopped (legacy behavior).
			LocalDeviceImpl* device = LocateDelegateFromMessage(message);
			BMessenger listener;
			bool hasListener
				= message->FindMessenger("listener", &listener) == B_OK;
			status = device != NULL
				? device->StopLEScan(hasListener ? &listener : NULL)
				: B_BAD_VALUE;
			break;
		}

		case BT_MSG_LE_CONNECT:
		{
			LocalDeviceImpl* device = LocateDelegateFromMessage(message);
			BMessenger listener;
			const void* address = NULL;
			ssize_t addressSize = 0;
			uint8 addressType;
			status = device != NULL
				&& message->FindMessenger("listener", &listener) == B_OK
				&& message->FindData("address", B_RAW_TYPE, &address,
					&addressSize) == B_OK
				&& addressSize == 6
				&& message->FindUInt8("address_type", &addressType) == B_OK
				? device->StartLEConnection(listener, (const uint8*)address,
					addressType, message->HasUInt16("interval_min")
						? message : NULL) : B_BAD_VALUE;
			break;
		}

		case BT_MSG_LE_CONNECT_CANCEL:
		{
			LocalDeviceImpl* device = LocateDelegateFromMessage(message);
			BMessenger listener;
			bool hasListener
				= message->FindMessenger("listener", &listener) == B_OK;
			status = device != NULL ? device->CancelLEConnection(
				hasListener ? &listener : NULL) : B_BAD_VALUE;
			break;
		}

		case BT_MSG_LE_DISCONNECT:
		{
			// "force" lets the preferences end a link another client owns,
			// for example after forgetting a connected mouse.
			LocalDeviceImpl* device = LocateDelegateFromMessage(message);
			BMessenger listener;
			bool hasListener
				= message->FindMessenger("listener", &listener) == B_OK
					&& !message->GetBool("force", false);
			status = device != NULL ? device->DisconnectLEConnection(
				hasListener ? &listener : NULL) : B_BAD_VALUE;
			break;
		}

		case BT_MSG_LE_UPDATE_CONNECTION:
		{
			LocalDeviceImpl* device = LocateDelegateFromMessage(message);
			status = device == NULL ? B_BAD_VALUE
				: device->UpdateLEConnection(
					message->GetUInt16("interval_min", 0),
					message->GetUInt16("interval_max", 0),
					message->GetUInt16("latency", 0xffff),
					message->GetUInt16("timeout", 0),
					message->GetBool("keep_interval", false));
			break;
		}

		case BT_MSG_READ_RSSI:
		{
			LocalDeviceImpl* device = LocateDelegateFromMessage(message);
			status = device == NULL ? B_BAD_VALUE
				: device->ReadRSSI(message->GetUInt16("handle", 0xffff) == 0xffff
					? -1 : message->GetUInt16("handle", 0));
			break;
		}

		case BT_MSG_LE_START_ENCRYPTION:
		{
			LocalDeviceImpl* device = LocateDelegateFromMessage(message);
			const void* key = NULL;
			ssize_t keySize = 0;
			if (device == NULL) {
				status = B_BAD_VALUE;
				break;
			}
			if (message->FindData("short_term_key", B_RAW_TYPE,
					&key, &keySize) == B_OK) {
				status = keySize == 16
					? device->StartLEEncryption((const uint8*)key, NULL, 0)
					: B_BAD_VALUE;
				break;
			}
			const void* randomNumber = NULL;
			ssize_t randomSize = 0;
			uint16 encryptedDiversifier;
			status = message->FindData("long_term_key", B_RAW_TYPE,
					&key, &keySize) == B_OK && keySize == 16
				&& message->FindData("random_number", B_RAW_TYPE,
					&randomNumber, &randomSize) == B_OK && randomSize == 8
				&& message->FindUInt16("encrypted_diversifier",
					&encryptedDiversifier) == B_OK
				? device->StartLEEncryption((const uint8*)key,
					(const uint8*)randomNumber, encryptedDiversifier)
				: B_BAD_VALUE;
			break;
		}

		case BT_MSG_GET_PROPERTY:
			status = HandleGetProperty(message, &reply);
			break;

		// Handle if the bluetooth preferences is running?
		case B_SOME_APP_LAUNCHED:
		{
			const char* signature;

			if (message->FindString("be:signature", &signature) == B_OK) {
				printf("input_server : %s\n", signature);
				if (strcmp(signature, "application/x-vnd.Be-TSKB") == 0) {

				}
			}
		}
		return;

		case BT_REQ_CREATE_CONN:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl == NULL)
				break;

			lDeviceImpl->CreateConnection(message);
			break;
		}
		case BT_MSG_PAIR_CONFIRM_RESULT:
		{
			LocalDeviceImpl* device = LocateDelegateFromMessage(message);
			const bdaddr_t* address;
			ssize_t length;
			int32 which = 0;
			if (device != NULL
				&& message->FindData("bdaddr", B_ANY_TYPE,
					(const void**)&address, &length) == B_OK
				&& length == sizeof(bdaddr_t)) {
				message->FindInt32("which", &which);
				device->ConfirmPairing(*address, which == 1);
			}
			break;
		}

		case BT_REQ_CANCEL_CONN:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl == NULL)
				break;

			lDeviceImpl->CancelConnection(message);
		}
		break;

		case BT_REQ_DISCONNECT:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl == NULL)
				break;

			lDeviceImpl->Disconnect(message);
		}
		break;

		case BT_REQ_REMOVE_DEVICE:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl == NULL)
				break;

			lDeviceImpl->Disconnect(message);

			const bdaddr_t* bdaddr;
			ssize_t addr_size;
			message->FindData("bdaddr", B_ANY_TYPE, (const void**)&bdaddr, &addr_size);

			ServerRemoteDevice* rd = lDeviceImpl->RemoteDeviceByAddr(*bdaddr);
			lDeviceImpl->RemoveRemoteDevice(rd);
			if (addr_size == sizeof(bdaddr_t))
				fHIDHost->RemoveDevice(*bdaddr);
		}
		break;

		case kMsgServicesDiscovered:
			_ServicesDiscovered(message);
			break;

		case kMsgCheckLinkSecurity:
		{
			LocalDeviceImpl* local = LocateDelegateFromMessage(message);
			const bdaddr_t* address;
			ssize_t size;
			if (local != NULL && message->FindData("bdaddr", B_ANY_TYPE,
					(const void**)&address, &size) == B_OK
				&& size == sizeof(bdaddr_t)) {
				local->SecureLink(*address, message->GetInt32("attempt", 0));
			}
			break;
		}

		case BT_START_WATCHING_CONNECTIONS:
		{
			BMessenger* messenger = new BMessenger();
			if (message->FindMessenger("messenger", messenger) == B_OK)
				fWatchersList.AddItem(messenger);
		}
		break;

		case BT_STOP_WATCHING_CONNECTIONS:
		{
			BMessenger* messenger = new BMessenger();
			if (message->FindMessenger("messenger", messenger) == B_OK)
				fWatchersList.RemoveItem(messenger);
		}
		break;

		case BT_MSG_GET_REMOTE_DEVICES:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl == NULL)
				break;

			RemoteDevicesList* rdList = lDeviceImpl->GetRemoteDevicesList();

			for (int32 i = 0; i < rdList->CountItems(); i++) {
				ServerRemoteDevice* rd = rdList->ItemAt(i);
				BMessage device;
				bdaddr_t bdaddr = rd->bdaddr;
				device.AddData("bdaddr", B_ANY_TYPE, &bdaddr, sizeof(bdaddr_t));
				device.AddString("name", rd->friendly_name);
				device.AddData("class_of_device", B_RAW_TYPE, rd->classOfDevice,
					sizeof(rd->classOfDevice));
				device.AddUInt16("clock_offset", rd->clock_offset);
				device.AddUInt8("pscan_rep_mode", rd->pscan_rep_mode);

				reply.AddMessage("remote", &device);
			}

			message->SendReply(&reply);
		}
		break;

		case BT_REQ_CONN_STATE:
		{
			LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
			if (lDeviceImpl == NULL)
				break;

			const bdaddr_t* bdaddr;
			ssize_t addr_size;
			ServerRemoteDevice* rd = NULL;
			if (message->FindData("bdaddr", B_ANY_TYPE,
					(const void**)&bdaddr, &addr_size) == B_OK
				&& addr_size == sizeof(bdaddr_t))
				rd = lDeviceImpl->RemoteDeviceByAddr(*bdaddr);
			reply.AddUInt8("conn state", static_cast<uint8>(rd != NULL
				? rd->conn_state : RemoteDevice::DISCONNECTED));
			if (rd != NULL && rd->conn_state == RemoteDevice::CONNECTED) {
				reply.AddBool("encrypted", rd->encryption_enabled != 0);
				reply.AddUInt16("handle", rd->handle);
			}

			message->SendReply(&reply);
		}
		break;

		default:
			BApplication::MessageReceived(message);
			break;
	}

	// Can we reply right now?
	// TODO: review this condition
	if (status != B_WOULD_BLOCK) {
		reply.AddInt32("status", status);
		message->SendReply(&reply);
//		printf("Sending reply message for->\n");
//		message->PrintToStream();
	}
}


void
BluetoothServer::NotifyWatchers(BMessage* notice)
{
	for (int32 i = 0; i < fWatchersList.CountItems(); i++) {
		BMessenger* messenger = fWatchersList.ItemAt(i);
		if (!messenger->IsValid()) {
			fWatchersList.RemoveItemAt(i);
			i--;
			continue;
		}
		messenger->SendMessage(notice);
	}
}


struct DiscoverServicesJob {
	bdaddr_t	address;
	BString		name;
	bool		outgoing;
	uint8		classOfDevice[3];
};


/*!	Called by the HCI event thread once a Classic link is authenticated.
	The SDP query takes seconds and waits on the very events that thread
	delivers, so it runs on a thread of its own and reports back with
	kMsgServicesDiscovered.
*/
void
BluetoothServer::DiscoverServices(ServerRemoteDevice* rd)
{
	TRACE_BT("BluetoothServer: Discovering Services for %s\n", rd->friendly_name.String());

	// A paired HID device: its records are known. One that woke up opens
	// its channels itself; one the user connected gets them opened.
	if (fHIDHost->IsHIDDevice(rd->bdaddr)) {
		if (rd->outgoing)
			fHIDHost->Connect(rd->bdaddr);
		return;
	}

	DiscoverServicesJob* job = new(std::nothrow) DiscoverServicesJob;
	if (job == NULL)
		return;
	job->address = rd->bdaddr;
	job->name = rd->friendly_name;
	job->outgoing = rd->outgoing;
	memcpy(job->classOfDevice, rd->classOfDevice, sizeof(job->classOfDevice));
	thread_id thread = spawn_thread(&_DiscoverServicesThread,
		"bluetooth SDP query", B_NORMAL_PRIORITY, job);
	if (thread < 0 || resume_thread(thread) != B_OK)
		delete job;
}


/*static*/ status_t
BluetoothServer::_DiscoverServicesThread(void* data)
{
	DiscoverServicesJob* job = (DiscoverServicesJob*)data;

	ServerRemoteDevice device;
	device.bdaddr = job->address;
	device.friendly_name = job->name;
	SDPClient sdpClient(&device);
	status_t status = sdpClient.Start();
	if (status == B_OK)
		status = sdpClient.RequestServiceRecords();
	sdpClient.Stop();

	char address[18];
	Bluetooth::LELog(status == B_OK ? Bluetooth::LE_LOG_INFO
			: Bluetooth::LE_LOG_ERROR, "classic",
		"SDP query of %s: %s", Bluetooth::LEAddressString(job->address.b,
			address), status == B_OK ? "done" : strerror(status));

	BMessage result(kMsgServicesDiscovered);
	result.AddData("bdaddr", B_ANY_TYPE, &job->address, sizeof(bdaddr_t));
	result.AddString("name", job->name);
	result.AddBool("outgoing", job->outgoing);
	result.AddData("class", B_RAW_TYPE, job->classOfDevice,
		sizeof(job->classOfDevice));
	result.AddInt32("status", status);
	result.AddMessage("services", &device.services);
	be_app->PostMessage(&result);

	delete job;
	return B_OK;
}


void
BluetoothServer::_ServicesDiscovered(BMessage* message)
{
	const bdaddr_t* address;
	ssize_t size;
	BMessage services;
	if (message->FindData("bdaddr", B_ANY_TYPE, (const void**)&address,
			&size) != B_OK || size != sizeof(bdaddr_t)
		|| message->FindMessage("services", &services) != B_OK)
		return;

	for (int32 i = 0; i < fLocalDevicesList.CountItems(); i++) {
		LocalDeviceImpl* local = fLocalDevicesList.ItemAt(i);
		ServerRemoteDevice* rd = local != NULL
			? local->RemoteDeviceByAddr(*address) : NULL;
		if (rd != NULL)
			rd->services = services;
	}

	// Without SDP records, a keyboard (its Class of Device: major class
	// Peripheral, minor keyboard bit) still works in the boot protocol.
	bool added = false;
	if (message->GetInt32("status", B_ERROR) == B_OK) {
		added = fHIDHost->AddDevice(*address, services,
			message->GetString("name", ""));
	} else {
		const uint8* deviceClass;
		ssize_t classSize;
		if (message->FindData("class", B_RAW_TYPE,
				(const void**)&deviceClass, &classSize) == B_OK
			&& classSize == 3 && (deviceClass[1] & 0x1f) == 0x05
			&& (deviceClass[0] & 0x40) != 0) {
			added = fHIDHost->AddBootKeyboard(*address,
				message->GetString("name", ""));
		}
	}
	if (added) {
		// Bonded HID devices page us when they wake up.
		for (int32 i = 0; i < fLocalDevicesList.CountItems(); i++) {
			LocalDeviceImpl* local = fLocalDevicesList.ItemAt(i);
			if (local != NULL)
				local->EnablePageScan();
		}
		// Opened from here whether we or the device made the link: a
		// keyboard that connected by itself may wait for us (the HID host
		// leaves alone a device that is opening its channels already).
		fHIDHost->Connect(*address);
	}
}


void
BluetoothServer::CheckLinkSecurityLater(hci_id hid, const bdaddr_t& address,
	int32 attempt)
{
	BMessage message(kMsgCheckLinkSecurity);
	message.AddInt32("hci_id", hid);
	message.AddData("bdaddr", B_ANY_TYPE, &address, sizeof(bdaddr_t));
	message.AddInt32("attempt", attempt);
	BMessageRunner::StartSending(be_app_messenger, &message,
		kLinkSecurityDelay, 1);
}


/*!	Called by the HCI event thread when a Classic link's encryption changes.
*/
void
BluetoothServer::NotifyServices(ServerRemoteDevice* rd)
{
	fHIDHost->LinkEncrypted(rd->bdaddr, rd->encryption_enabled != 0,
		!rd->outgoing);
}


#if 0
#pragma mark -
#endif

LocalDeviceImpl*
BluetoothServer::LocateDelegateFromMessage(BMessage* message)
{
	hci_id hid;

	if (message->FindInt32("hci_id", &hid) != B_OK)
		return NULL;

	return LocateLocalDeviceImpl(hid);
}


LocalDeviceImpl*
BluetoothServer::LocateLocalDeviceImpl(hci_id hid)
{
	// Try to find out when a ID was specified
	int index;

	for (index = 0; index < fLocalDevicesList.CountItems(); index++) {
		LocalDeviceImpl* lDeviceImpl = fLocalDevicesList.ItemAt(index);
		if (lDeviceImpl->GetID() == hid)
			return lDeviceImpl;
	}

	return NULL;
}


#if 0
#pragma - Messages reply
#endif

status_t
BluetoothServer::HandleLocalDevicesCount(BMessage* message, BMessage* reply)
{
	TRACE_BT("BluetoothServer: count requested\n");

	return reply->AddInt32("count", fLocalDevicesList.CountItems());
}


status_t
BluetoothServer::HandleAcquireLocalDevice(BMessage* message, BMessage* reply)
{
	hci_id hid;
	ssize_t size;
	bdaddr_t bdaddr;
	LocalDeviceImpl* lDeviceImpl = NULL;
	static int32 lastIndex = 0;

	if (message->FindInt32("hci_id", &hid) == B_OK)	{
		TRACE_BT("BluetoothServer: GetLocalDevice requested with id\n");
		lDeviceImpl = LocateDelegateFromMessage(message);

	} else if (message->FindData("bdaddr", B_ANY_TYPE,
		(const void**)&bdaddr, &size) == B_OK) {

		// Try to find out when the user specified the address
		TRACE_BT("BluetoothServer: GetLocalDevice requested with bdaddr\n");
		for (lastIndex = 0; lastIndex < fLocalDevicesList.CountItems();
			lastIndex ++) {
			// TODO: Only possible if the property is available
			// bdaddr_t local;
			// lDeviceImpl = fLocalDevicesList.ItemAt(lastIndex);
			// if ((lDeviceImpl->GetAddress(&local, message) == B_OK)
			// 	&& bacmp(&local, &bdaddr)) {
			// 	break;
			// }
		}

	} else {
		// Careless, any device not performing operations will be fine
		TRACE_BT("BluetoothServer: GetLocalDevice plain request\n");
		// from last assigned till end
		for (int index = lastIndex + 1;
			index < fLocalDevicesList.CountItems();	index++) {
			lDeviceImpl= fLocalDevicesList.ItemAt(index);
			if (lDeviceImpl != NULL && lDeviceImpl->Available()) {
				printf("Requested local device %" B_PRId32 "\n",
					lDeviceImpl->GetID());
				TRACE_BT("BluetoothServer: Device available: %" B_PRId32 "\n", lDeviceImpl->GetID());
				lastIndex = index;
				break;
			}
		}

		// from starting till last assigned if not yet found
		if (lDeviceImpl == NULL) {
			for (int index = 0; index <= lastIndex ; index ++) {
				lDeviceImpl = fLocalDevicesList.ItemAt(index);
				if (lDeviceImpl != NULL && lDeviceImpl->Available()) {
					printf("Requested local device %" B_PRId32 "\n",
						lDeviceImpl->GetID());
					TRACE_BT("BluetoothServer: Device available: %" B_PRId32 "\n", lDeviceImpl->GetID());
					lastIndex = index;
					break;
				}
			}
		}
	}

	if (lastIndex <= fLocalDevicesList.CountItems() && lDeviceImpl != NULL
		&& lDeviceImpl->Available()) {

		hid = lDeviceImpl->GetID();
		lDeviceImpl->Acquire();

		TRACE_BT("BluetoothServer: Device acquired %" B_PRId32 "\n", hid);
		return reply->AddInt32("hci_id", hid);
	}

	return B_ERROR;

}


status_t
BluetoothServer::HandleSimpleRequest(BMessage* message, BMessage* reply)
{
	LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
	if (lDeviceImpl == NULL) {
		return B_ERROR;
	}

	const char* propertyRequested;

	// Find out if there is a property being requested,
	if (message->FindString("property", &propertyRequested) == B_OK) {
		// Check if the property has been already retrieved
		if (lDeviceImpl->IsPropertyAvailable(propertyRequested)) {
			// Dump everything
			reply->AddMessage("properties", lDeviceImpl->GetPropertiesMessage());
			return B_OK;
		}
	}

	// we are gonna need issue the command ...
	if (lDeviceImpl->ProcessSimpleRequest(DetachCurrentMessage()) == B_OK)
		return B_WOULD_BLOCK;
	else {
		lDeviceImpl->Unregister();
		return B_ERROR;
	}

}


status_t
BluetoothServer::HandleGetProperty(BMessage* message, BMessage* reply)
{
	// User side will look for the reply in a result field and will
	// not care about status fields, therefore we return OK in all cases

	LocalDeviceImpl* lDeviceImpl = LocateDelegateFromMessage(message);
	if (lDeviceImpl == NULL) {
		return B_ERROR;
	}

	const char* propertyRequested;

	// Find out if there is a property being requested,
	if (message->FindString("property", &propertyRequested) == B_OK) {

		TRACE_BT("BluetoothServer: Searching %s property...\n", propertyRequested);

		// Check if the property has been already retrieved
		if (lDeviceImpl->IsPropertyAvailable(propertyRequested)) {

			// 1 bytes requests
			if (strcmp(propertyRequested, "hci_version") == 0
				|| strcmp(propertyRequested, "lmp_version") == 0
				|| strcmp(propertyRequested, "sco_mtu") == 0) {

				uint8 result = lDeviceImpl->GetPropertiesMessage()->
					FindInt8(propertyRequested);
				reply->AddInt32("result", result);

			// 2 bytes requests
			} else if (strcmp(propertyRequested, "hci_revision") == 0
					|| strcmp(propertyRequested, "lmp_subversion") == 0
					|| strcmp(propertyRequested, "manufacturer") == 0
					|| strcmp(propertyRequested, "acl_mtu") == 0
					|| strcmp(propertyRequested, "acl_max_pkt") == 0
					|| strcmp(propertyRequested, "sco_max_pkt") == 0
					|| strcmp(propertyRequested, "packet_type") == 0 ) {

				uint16 result = lDeviceImpl->GetPropertiesMessage()->
					FindInt16(propertyRequested);
				reply->AddInt32("result", result);

			// 1 bit requests
			} else if (strcmp(propertyRequested, "role_switch_capable") == 0
					|| strcmp(propertyRequested, "encrypt_capable") == 0) {

				bool result = lDeviceImpl->GetPropertiesMessage()->
					FindBool(propertyRequested);

				reply->AddInt32("result", result);



			} else {
				TRACE_BT("BluetoothServer: Property %s could not be satisfied\n", propertyRequested);
			}
		}
	}

	return B_OK;
}


#if 0
#pragma mark -
#endif


void
BluetoothServer::ShowWindow(BWindow* pWindow)
{
	pWindow->Lock();
	if (pWindow->IsHidden())
		pWindow->Show();
	else
		pWindow->Activate();
	pWindow->Unlock();
}


#if 0
#pragma mark -
#endif

int
main(int /*argc*/, char** /*argv*/)
{
	LogStartup("starting");
	BluetoothServer* bluetoothServer = new BluetoothServer;

	bluetoothServer->Run();
	delete bluetoothServer;

	return 0;
}
