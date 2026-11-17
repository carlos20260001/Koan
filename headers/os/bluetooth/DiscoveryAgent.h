/*
 * Copyright 2007-2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2026, Vitruvian Project.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Backed by BlueZBackend instead of raw HCI inquiry commands:
 *  - The access code (GIAC/LIAC) of air/OS's StartInquiry() selects an HCI
 *    inquiry access code; BlueZ's StartDiscovery always does a general
 *    inquiry, so the code is accepted and ignored.
 *  - StartInquiry() only queues the request; results arrive at the
 *    listener. Discovery stops by itself after the inquiry time.
 *  - VitruvianOS's agents are made for an adapter path (public
 *    constructor), air/OS's through LocalDevice::GetDiscoveryAgent().
 */
#ifndef _DISCOVERY_AGENT_H
#define _DISCOVERY_AGENT_H

#include <String.h>
#include <SupportDefs.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/RemoteDevice.h>


#define BT_CACHED 0x00
#define BT_PREKNOWN 0x01
#define BT_NOT_DISCOVERABLE 0x01

#define BT_GIAC 0x9E8B33
#define BT_LIAC 0x9E8B00

#define BT_MAX_RESPONSES		(32)

#define BT_BASE_INQUIRY_TIME	(1.28)
#define BT_DEFAULT_INQUIRY_TIME	(0x0A)
#define BT_MIN_INQUIRY_TIME	(0x01) //  1.18 secs
#define BT_MAX_INQUIRY_TIME	(0x30) // 61.44 secs

// Default/bounds for the inquiry window in seconds -- the range above,
// expressed in seconds instead of 1.28 s HCI inquiry ticks.
#define BT_DEFAULT_INQUIRY_TIME_SECS	10
#define BT_MIN_INQUIRY_TIME_SECS		1
#define BT_MAX_INQUIRY_TIME_SECS		61

class BMessageRunner;

namespace Bluetooth {

class LocalDevice;

class DiscoveryAgent {

public:

	static const int GIAC = BT_GIAC;
	static const int LIAC = BT_LIAC;

	static const int PREKNOWN = BT_PREKNOWN;
	static const int CACHED = BT_CACHED;
	static const int NOT_DISCOVERABLE = BT_NOT_DISCOVERABLE;

	explicit DiscoveryAgent(const BString& adapterPath);
	~DiscoveryAgent();

	RemoteDevicesList RetrieveDevices(int option); /* TODO */
	status_t StartInquiry(int accessCode, DiscoveryListener* listener); /* Throwing */
	status_t StartInquiry(uint32 accessCode, DiscoveryListener* listener, bigtime_t secs);

	// Starts BlueZ discovery on this agent's adapter. NOTIFICATION_DEVICE_FOUND
	// is routed to `listener` for `secs` microseconds, after which discovery
	// is stopped automatically and listener->InquiryCompleted(INQUIRY_COMPLETED)
	// fires. BlueZBackend supports any number of concurrent watchers, each
	// with its own mask, so a scan's DEVICE_FOUND registration coexists with
	// other property-change watchers (e.g. the preflet's own StartWatching).
	status_t StartInquiry(DiscoveryListener* listener,
		bigtime_t secs = BT_DEFAULT_INQUIRY_TIME_SECS * 1000000LL);
	status_t CancelInquiry(DiscoveryListener* listener);

	/*
	int searchServices(int[] attrSet, UUID[] uuidSet, RemoteDevice btDev,
					DiscoveryListener discListener);

	bool cancelServiceSearch(int transID);
	BString selectService(UUID uuid, int security, boolean master);
	*/

private:
	DiscoveryAgent(LocalDevice* ld);
	void SetLocalDeviceOwner(LocalDevice* ld);

	DiscoveryListener* fLastUsedListener;
	LocalDevice*       fLocalDevice;

	BString fAdapterPath;
	DiscoveryListener* fListener;
	BMessageRunner* fTimeoutRunner;

	friend class LocalDevice;
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::DiscoveryAgent;
#endif

#endif // _DISCOVERY_AGENT_H
