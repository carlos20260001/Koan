/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/LocalDevice.h>

#include <MessageRunner.h>
#include <Messenger.h>

#include <new>

#include "BlueZBackend.h"


namespace Bluetooth {


DiscoveryAgent::DiscoveryAgent(const BString& adapterPath)
	:
	fLastUsedListener(NULL),
	fLocalDevice(NULL),
	fAdapterPath(adapterPath),
	fListener(NULL),
	fTimeoutRunner(NULL)
{
}


DiscoveryAgent::DiscoveryAgent(LocalDevice* ld)
	:
	fLastUsedListener(NULL),
	fLocalDevice(ld),
	fListener(NULL),
	fTimeoutRunner(NULL)
{
	if (ld != NULL)
		fAdapterPath = ld->Path();
}


void
DiscoveryAgent::SetLocalDeviceOwner(LocalDevice* ld)
{
	fLocalDevice = ld;
	if (ld != NULL && fAdapterPath.IsEmpty())
		fAdapterPath = ld->Path();
}


/*!	CACHED: the devices the last inquiry found; PREKNOWN: the devices
	paired with this adapter. The caller owns neither list's devices for
	CACHED (the listener does) and owns them for PREKNOWN.
*/
RemoteDevicesList
DiscoveryAgent::RetrieveDevices(int option)
{
	RemoteDevicesList list;

	if (option == CACHED) {
		if (fLastUsedListener != NULL)
			list = fLastUsedListener->GetRemoteDevicesList();
		return list;
	}

	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return list;

	BMessage devices;
	int32 count = 0;
	if (backend->GetDevices(&devices) != B_OK
		|| devices.FindInt32("device_count", &count) != B_OK)
		return list;

	for (int32 i = 0; i < count; i++) {
		BString itemName;
		itemName << "device_" << i;

		BMessage info;
		bool paired = false;
		const char* adapter = NULL;
		if (devices.FindMessage(itemName.String(), &info) != B_OK
			|| info.FindBool("paired", &paired) != B_OK || !paired)
			continue;
		if (!fAdapterPath.IsEmpty()
			&& info.FindString("adapter", &adapter) == B_OK
			&& fAdapterPath != adapter)
			continue;

		RemoteDevice* device = new(std::nothrow) RemoteDevice(info);
		if (device == NULL)
			break;
		device->SetLocalDeviceOwner(fLocalDevice);
		list.AddItem(device);
	}

	return list;
}


status_t
DiscoveryAgent::StartInquiry(int accessCode, DiscoveryListener* listener)
{
	return StartInquiry((uint32)accessCode, listener,
		BT_DEFAULT_INQUIRY_TIME_SECS);
}


/*!	air/OS's form: \a secs is in seconds. BlueZ always does a general
	inquiry, so \a accessCode is not used.
*/
status_t
DiscoveryAgent::StartInquiry(uint32 accessCode, DiscoveryListener* listener,
	bigtime_t secs)
{
	if (secs < BT_MIN_INQUIRY_TIME_SECS || secs > BT_MAX_INQUIRY_TIME_SECS)
		return B_TIMED_OUT;

	if (listener != NULL)
		listener->SetLocalDeviceOwner(fLocalDevice);

	return StartInquiry(listener, secs * 1000000LL);
}


DiscoveryAgent::~DiscoveryAgent()
{
	if (fListener != NULL)
		CancelInquiry(fListener);
}


status_t
DiscoveryAgent::StartInquiry(DiscoveryListener* listener, bigtime_t secs)
{
	if (listener == NULL || fAdapterPath.IsEmpty())
		return B_BAD_VALUE;

	if (secs < BT_MIN_INQUIRY_TIME_SECS * 1000000LL)
		secs = BT_MIN_INQUIRY_TIME_SECS * 1000000LL;
	else if (secs > BT_MAX_INQUIRY_TIME_SECS * 1000000LL)
		secs = BT_MAX_INQUIRY_TIME_SECS * 1000000LL;

	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	if (fListener != NULL)
		CancelInquiry(fListener);

	fListener = listener;
	fLastUsedListener = listener;

	// Claims BlueZBackend's single watcher slot for the scan's duration --
	// see the class comment in DiscoveryAgent.h.
	backend->StartWatching(BMessenger(listener),
		BlueZBackend::NOTIFICATION_DEVICE_FOUND);

	backend->StartDiscoveryAsync(fAdapterPath.String(), BMessenger(listener),
		DiscoveryListener::kInquiryStartAckWhat);

	// Unlike real HCI hardware, BlueZ's StartDiscovery keeps scanning until
	// explicitly stopped -- the timeout message carries the adapter path so
	// DiscoveryListener can stop it before reporting INQUIRY_COMPLETED.
	BMessage* timeoutMessage = new BMessage(DiscoveryListener::kInquiryTimeoutWhat);
	timeoutMessage->AddString("adapter_path", fAdapterPath);

	delete fTimeoutRunner;
	fTimeoutRunner = new BMessageRunner(BMessenger(listener), timeoutMessage,
		secs, 1);

	return B_OK;
}


status_t
DiscoveryAgent::CancelInquiry(DiscoveryListener* listener)
{
	if (listener == NULL || fAdapterPath.IsEmpty())
		return B_BAD_VALUE;

	delete fTimeoutRunner;
	fTimeoutRunner = NULL;

	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	backend->StopDiscoveryAsync(fAdapterPath.String(), BMessenger(listener),
		DiscoveryListener::kInquiryTerminatedWhat);
	backend->StopWatching(BMessenger(listener));

	if (fListener == listener)
		fListener = NULL;

	return B_OK;
}


} // namespace Bluetooth
