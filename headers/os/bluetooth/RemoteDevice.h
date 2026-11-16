/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2026, Vitruvian Project.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Backed by BlueZBackend (org.bluez over D-Bus) instead of raw HCI through
 * bluetooth_server. air/OS's (Haiku's) API stays; VitruvianOS's
 * asynchronous API, keyed by the BlueZ device object path, sits next to it:
 *  - Connect/Disconnect/Pair/Unpair with a BMessenger are async: each is a
 *    D-Bus round trip that must not block a window thread (see
 *    BlueZBackend.h's async request/reply note).
 *  - Devices made by DevicesFromMessage() read a cached snapshot populated
 *    from a BlueZBackend::GetDevicesAsync()/GetStatusAsync() reply.
 * VitruvianOS's GetBluetoothAddress() returning the address as a string is
 * GetBluetoothAddressString() here; GetBluetoothAddress() returns bdaddr_t.
 */
#ifndef _REMOTE_DEVICE_H
#define _REMOTE_DEVICE_H

#include <bluetooth/bluetooth.h>
#include <bluetooth/bluetooth_error.h>
#include <bluetooth/BluetoothDevice.h>

#include <Messenger.h>
#include <ObjectList.h>
#include <String.h>

#define B_BT_WAIT 0x00
#define B_BT_SUCCEEDED 0x01

class BMessage;


namespace Bluetooth {

class Connection;
class LocalDevice;

class RemoteDevice : public BluetoothDevice {

public:
	static const int WAIT = B_BT_WAIT;
	static const int SUCCEEDED = B_BT_SUCCEEDED;

	enum ConnectionState {
		CONNECTED,
		CONNECTING,
		DISCONNECTED
	};

	/* air/OS API */
	static BObjectList<RemoteDevice> GetRemoteDevices(LocalDevice* localDevice);
	static RemoteDevice* ParseRemoteDevice(LocalDevice* localDevice, BMessage* message);

	virtual 		~RemoteDevice();

	bool 			IsTrustedDevice() const;
	BString 		GetFriendlyName(bool alwaysAsk); /* Throwing */
	virtual BString	GetFriendlyName(void); /* Throwing */
	BString			GetFriendlyName() const;
	BString 		GetCachedFriendlyName();
	virtual bdaddr_t GetBluetoothAddress();
	uint8 			GetPageRepetitionMode();
	uint16 			GetClockOffset();
	virtual DeviceClass GetDeviceClass();
	DeviceClass		GetDeviceClass() const;

	bool 			Equals(RemoteDevice* obj) const;

	status_t		Connect();
	status_t		CancelConnection();
	status_t		Disconnect(bool removeDevice);

	/*static RemoteDevice* GetRemoteDevice(Connection conn);   Throwing */
	/* bool Authorize(Connection conn);  Throwing */
	/*bool Encrypt(Connection conn, bool on);  Throwing */
	/*bool IsAuthorized(Connection conn);  Throwing */
	RemoteDevice::ConnectionState 	GetConnectionState();
	bool 			IsEncrypted(); /* Throwing */

	virtual BString	GetProperty(const char* property); /* Throwing */
	virtual status_t GetProperty(const char* property, uint32* value); /* Throwing */

	LocalDevice* 	GetLocalDeviceOwner();

					RemoteDevice(const BString& address);
					RemoteDevice(const bdaddr_t address, uint8 record[3]);

	/* VitruvianOS API */
	// Thin wrapper for BlueZBackend::GetDevicesAsync() -- keeps
	// BlueZBackend.h out of consumers entirely.
	static status_t FetchAllAsync(const BMessenger& replyTo, uint32 replyWhat);

	// Populates outList from a BlueZBackend::GetDevices()-shaped BMessage
	// (device_count/device_N). outList owns the RemoteDevice instances it
	// receives.
	static status_t DevicesFromMessage(const BMessage& devicesReply,
		BObjectList<RemoteDevice, true>& outList);

	explicit RemoteDevice(const BMessage& deviceInfo);

	const BString& Path() const { return fPath; }
	const BString& AdapterPath() const { return fAdapterPath; }
	BString GetBluetoothAddressString() const { return fAddress; }

	bool IsPaired() const { return fPaired; }
	bool IsConnected() const { return fConnected; }
	bool IsBlockedDevice() const { return fBlocked; }

	status_t Connect(const BMessenger& replyTo, uint32 replyWhat) const;
	status_t Disconnect(const BMessenger& replyTo, uint32 replyWhat) const;
	status_t Pair(const BMessenger& replyTo, uint32 replyWhat) const;
	status_t Unpair(const BMessenger& replyTo, uint32 replyWhat) const;

	// org.bluez.Device1 Trusted/Blocked. See BlueZBackend.h's comment on
	// SetDeviceTrusted for how Trusted affects the pairing agent.
	status_t SetTrusted(bool trusted, const BMessenger& replyTo,
		uint32 replyWhat) const;
	status_t SetBlocked(bool blocked, const BMessenger& replyTo,
		uint32 replyWhat) const;

protected:
	/* called by Discovery[Listener|Agent] */
	void 			SetLocalDeviceOwner(LocalDevice* ld);
	friend class DiscoveryListener;
	friend class DiscoveryAgent;

private:
	void			_SetFromInfo(const BMessage& deviceInfo);
	status_t		_ResolvePath();
	BString			_FriendlyName() const;

	LocalDevice* 	fDiscovererLocalDevice;

	uint8			fPageRepetitionMode;
	uint8			fScanPeriodMode;
	uint8			fScanMode;
	uint16			fClockOffset;
	int8        	fRSSI;
	BString     	fFriendlyName;
	bool        	fFriendlyNameIsComplete;

	BString			fPath;
	BString			fAdapterPath;
	BString			fAddress;
	BString			fName;
	BString			fAlias;
	bool			fPaired;
	bool			fConnected;
	bool			fTrusted;
	bool			fBlocked;
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::RemoteDevice;
#endif

#endif // _REMOTE_DEVICE_H
