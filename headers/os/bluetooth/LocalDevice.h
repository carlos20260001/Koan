/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2026, Vitruvian Project.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Backed by BlueZBackend (org.bluez over D-Bus) instead of raw HCI through
 * bluetooth_server. Two APIs live here:
 *  - air/OS's (Haiku's) synchronous API: GetLocalDevice(), the friendly
 *    name, class and discoverability calls, keyed by hci_id/bdaddr_t. An
 *    hci_id is the adapter's index in BlueZ's adapter list.
 *  - VitruvianOS's asynchronous API: adapters identified by their BlueZ
 *    object path, every request answered through a BMessenger, so a window
 *    thread never waits on a D-Bus round trip. Prefer it in GUI code.
 * VitruvianOS's GetBluetoothAddress() returning the address as a string is
 * GetBluetoothAddressString() here; GetBluetoothAddress() returns bdaddr_t.
 */
#ifndef _LOCAL_DEVICE_H
#define _LOCAL_DEVICE_H

#include <bluetooth/bluetooth.h>
#include <bluetooth/DeviceClass.h>
#include <bluetooth/BluetoothDevice.h>

#include <bluetooth/HCI/btHCI.h>

#include <Messenger.h>
#include <Message.h>
#include <ObjectList.h>

#include <String.h>


namespace Bluetooth {

class DiscoveryAgent;

class LocalDevice;
typedef BObjectList<LocalDevice, true> LocalDevicesList;

class LocalDevice : public BluetoothDevice {

public:
	// Mirrors BlueZBackend::NotificationType exactly -- these are the literal
	// `what` codes BlueZBackend posts to a watching BMessenger, so the
	// values must match; this is a pass-through, not a translation layer.
	enum NotificationType {
		NOTIFICATION_ADAPTER_ADDED = 'BTAD',
		NOTIFICATION_ADAPTER_REMOVED = 'BTRM',
		NOTIFICATION_ADAPTER_PROPERTY_CHANGED = 'BTAP',
		NOTIFICATION_DEVICE_FOUND = 'BTDF',
		NOTIFICATION_DEVICE_CONNECTED = 'BTDC',
		NOTIFICATION_DEVICE_DISCONNECTED = 'BTDD',
		NOTIFICATION_DEVICE_PROPERTY_CHANGED = 'BTDP'
	};

	// org.bluez.Agent1, capability KeyboardDisplay -- see BlueZBackend.h for
	// the full contract. uiHandler receives kAgentRequest ("request_id",
	// "kind" a pairing_dialog_kind value, "device_name", per-kind fields)
	// and kAgentCancel; both calls return immediately, safe from a window
	// thread. Register/unregister is idempotent and reentry-safe so it can
	// be driven straight from AttachedToWindow()/DetachedFromWindow().
	enum {
		kAgentRequest = 'BTAR',
		kAgentCancel = 'BTAC'
	};

	/* air/OS API (synchronous) */
	static	LocalDevice*	GetLocalDevice();
	static	uint32			GetLocalDeviceCount();

	static	LocalDevice*	GetLocalDevice(const hci_id hid);
	static	LocalDevice*	GetLocalDevice(const bdaddr_t bdaddr);

			// Caller owns the returned agent.
			DiscoveryAgent*	GetDiscoveryAgent() const;
	virtual	BString			GetFriendlyName();
			BString			GetFriendlyName() const;
			status_t		SetFriendlyName(BString& name);
	virtual	DeviceClass		GetDeviceClass();
			DeviceClass		GetDeviceClass() const;
			status_t		SetDeviceClass(DeviceClass deviceClass);

			status_t		SetDiscoverable(int mode);
			status_t		SetAuthentication(bool authentication);

	virtual	BString			GetProperty(const char* property);
	virtual	status_t 		GetProperty(const char* property, uint32* value);

			int				GetDiscoverable();
	virtual	bdaddr_t		GetBluetoothAddress();
	/*
			ServiceRecord getRecord(Connection notifier);
			void updateRecord(ServiceRecord srvRecord);
	*/
			hci_id			ID() const;

	/* VitruvianOS API (asynchronous) */
	// Thin wrappers for BlueZBackend's watcher registration and adapter/
	// device enumeration -- keep BlueZBackend.h out of consumers entirely.
	static	status_t		StartWatching(const BMessenger& target,
								uint32 notificationMask);
	static	status_t		StopWatching(const BMessenger& target);

	static	status_t		RegisterAgent(const BMessenger& uiHandler,
								const BMessenger& replyTo, uint32 replyWhat);
	static	status_t		UnregisterAgent(const BMessenger& replyTo,
								uint32 replyWhat);
	static	void			CompleteAgentRequest(uint32 requestId,
								bool accepted, const BString& value);

	static	status_t		FetchAllAsync(const BMessenger& replyTo,
								uint32 replyWhat);

	// Combined adapters+devices snapshot in one round trip; reply carries
	// "adapters" and "devices" sub-BMessages, each in DevicesFromMessage()/
	// RemoteDevice::DevicesFromMessage() shape.
	static	status_t		FetchStatusAsync(const BMessenger& replyTo,
								uint32 replyWhat);

	// Populates outList from the resulting BMessage (adapter_count/adapter_N
	// shape). outList owns the LocalDevice instances it receives.
	static	status_t		DevicesFromMessage(const BMessage& adaptersReply,
								LocalDevicesList& outList);

	explicit				LocalDevice(const BMessage& adapterInfo);
	virtual					~LocalDevice();

			const BString&	Path() const { return fPath; }
			BString			GetBluetoothAddressString() const
								{ return fAddress; }
			bool			IsPowered() const { return fPowered; }
			bool			IsDiscoverable() const { return fDiscoverable; }
			bool			IsDiscovering() const { return fDiscovering; }
	// org.bluez.Adapter1.Pairable: honest mapping for "incoming connections
	// policy" -- BlueZ has no multi-level policy, only this on/off gate on
	// whether the adapter accepts incoming pairing requests at all.
			bool			IsPairable() const { return fPairable; }

			status_t		SetPowered(bool powered, const BMessenger& replyTo,
								uint32 replyWhat) const;
			status_t		SetDiscoverable(bool discoverable,
								const BMessenger& replyTo,
								uint32 replyWhat) const;
			status_t		SetPairable(bool pairable,
								const BMessenger& replyTo,
								uint32 replyWhat) const;
			status_t		StartDiscovery(const BMessenger& replyTo,
								uint32 replyWhat) const;
			status_t		StopDiscovery(const BMessenger& replyTo,
								uint32 replyWhat) const;

private:
			void			_Refresh();

			BString			fPath;
			BString			fAddress;
			BString			fName;
			bool			fPowered;
			bool			fDiscoverable;
			bool			fDiscovering;
			bool			fPairable;
			hci_id			fHid;

	friend class DiscoveryAgent;
	friend class RemoteDevice;
	friend class PincodeWindow;
};

}


#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::LocalDevice;
using Bluetooth::LocalDevicesList;
#endif

#endif // _LOCAL_DEVICE_H
