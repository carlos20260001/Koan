/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2026, Vitruvian Project.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Backed by BlueZBackend. A BLooper with its own thread, so a listener fits
 * the asynchronous D-Bus backend without a synchronous rendezvous; see
 * DiscoveryListener.cpp for which thread the hooks run on.
 * InquiryCompleted() is VitruvianOS's name for air/OS's InquiryResponse();
 * the kit calls InquiryCompleted(), whose default calls InquiryResponse(),
 * so a subclass may override either.
 */
#ifndef _DISCOVERY_LISTENER_H
#define _DISCOVERY_LISTENER_H

#include <Looper.h>
#include <ObjectList.h>

#include <bluetooth/DeviceClass.h>
#include <bluetooth/RemoteDevice.h>


#define BT_INQUIRY_COMPLETED	0x01
#define BT_INQUIRY_TERMINATED	0x02
#define BT_INQUIRY_ERROR		0x03


namespace Bluetooth {

typedef BObjectList<RemoteDevice> RemoteDevicesList;

class LocalDevice;
class DiscoveryAgent;

class DiscoveryListener : public BLooper {

public:

	static const int INQUIRY_COMPLETED = BT_INQUIRY_COMPLETED;
	static const int INQUIRY_TERMINATED = BT_INQUIRY_TERMINATED;
	static const int INQUIRY_ERROR = BT_INQUIRY_ERROR;

	static const int SERVICE_SEARCH_COMPLETED = 0x01;
	static const int SERVICE_SEARCH_TERMINATED = 0x02;
	static const int SERVICE_SEARCH_ERROR = 0x03;
	static const int SERVICE_SEARCH_NO_RECORDS = 0x04;
	static const int SERVICE_SEARCH_DEVICE_NOT_REACHABLE = 0x06;

	DiscoveryListener();

	virtual void DeviceDiscovered(RemoteDevice* btDevice, DeviceClass cod);
	/*
	virtual void servicesDiscovered(int transID, ServiceRecord[] servRecord);
	virtual void serviceSearchCompleted(int transID, int respCode);
	*/
	virtual void InquiryResponse(int discType);
	virtual void InquiryCompleted(int discType);

	/* JSR82 non-defined methods */
	virtual void InquiryStarted(status_t status);

private:

	RemoteDevicesList GetRemoteDevicesList(void);

	virtual void MessageReceived(BMessage* message);
	void _ReportKnownDevices();

	// Private protocol between DiscoveryAgent and its listener; BlueZBackend's
	// own NOTIFICATION_DEVICE_FOUND is handled directly (the agent points
	// BlueZBackend's watcher straight at the listener, see DiscoveryAgent.cpp).
	static const uint32 kInquiryStartAckWhat = 'btSA';
	static const uint32 kInquiryTimeoutWhat = 'btIT';
	static const uint32 kInquiryTerminatedWhat = 'btIX';

	LocalDevice* fLocalDevice;
	BObjectList<RemoteDevice, true> fRemoteDevicesList;

	friend class DiscoveryAgent;

protected:
	void SetLocalDeviceOwner(LocalDevice* ld);
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::DiscoveryListener;
using Bluetooth::RemoteDevicesList;
#endif

#endif // _DISCOVERY_LISTENER_H
