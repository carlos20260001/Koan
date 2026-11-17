/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <bluetooth/LocalDevice.h>
#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/bdaddrUtils.h>

#include <Message.h>

#include <new>
#include <string.h>

#include "BlueZBackend.h"


namespace Bluetooth {


status_t
LocalDevice::StartWatching(const BMessenger& target, uint32 notificationMask)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->StartWatching(target, notificationMask);
}


status_t
LocalDevice::StopWatching(const BMessenger& target)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->StopWatching(target);
}


status_t
LocalDevice::RegisterAgent(const BMessenger& uiHandler,
	const BMessenger& replyTo, uint32 replyWhat)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->RegisterAgentAsync(uiHandler, replyTo, replyWhat);
}


status_t
LocalDevice::UnregisterAgent(const BMessenger& replyTo, uint32 replyWhat)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->UnregisterAgentAsync(replyTo, replyWhat);
}


void
LocalDevice::CompleteAgentRequest(uint32 requestId, bool accepted,
	const BString& value)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return;

	backend->CompleteAgentRequest(requestId, accepted, value);
}


status_t
LocalDevice::FetchAllAsync(const BMessenger& replyTo, uint32 replyWhat)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->GetAdaptersAsync(replyTo, replyWhat);
}


status_t
LocalDevice::FetchStatusAsync(const BMessenger& replyTo, uint32 replyWhat)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->GetStatusAsync(replyTo, replyWhat);
}


status_t
LocalDevice::DevicesFromMessage(const BMessage& adaptersReply,
	LocalDevicesList& outList)
{
	int32 count = 0;
	if (adaptersReply.FindInt32("adapter_count", &count) != B_OK)
		return B_BAD_VALUE;

	for (int32 i = 0; i < count; i++) {
		BString name;
		name << "adapter_" << i;

		BMessage info;
		if (adaptersReply.FindMessage(name.String(), &info) != B_OK)
			continue;

		outList.AddItem(new LocalDevice(info));
	}

	return B_OK;
}


LocalDevice::LocalDevice(const BMessage& adapterInfo)
	:
	fPowered(false),
	fDiscoverable(false),
	fDiscovering(false),
	fPairable(false),
	fHid(-1)
{
	const char* path;
	if (adapterInfo.FindString("path", &path) == B_OK)
		fPath = path;

	const char* address;
	if (adapterInfo.FindString("address", &address) == B_OK)
		fAddress = address;

	const char* name;
	if (adapterInfo.FindString("alias", &name) == B_OK)
		fName = name;
	else if (adapterInfo.FindString("name", &name) == B_OK)
		fName = name;

	uint32 classRecord;
	if (adapterInfo.FindUInt32("class", &classRecord) == B_OK)
		fDeviceClass.SetRecord(classRecord);

	adapterInfo.FindBool("powered", &fPowered);
	adapterInfo.FindBool("discoverable", &fDiscoverable);
	adapterInfo.FindBool("discovering", &fDiscovering);
	adapterInfo.FindBool("pairable", &fPairable);

	if (!fAddress.IsEmpty())
		fBdaddr = bdaddrUtils::FromString(fAddress.String());
}


LocalDevice::~LocalDevice()
{
}


status_t
LocalDevice::SetPowered(bool powered, const BMessenger& replyTo,
	uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->SetAdapterPoweredAsync(fPath.String(), powered, replyTo,
		replyWhat);
}


status_t
LocalDevice::SetDiscoverable(bool discoverable, const BMessenger& replyTo,
	uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->SetAdapterDiscoverableAsync(fPath.String(), discoverable,
		replyTo, replyWhat);
}


status_t
LocalDevice::SetPairable(bool pairable, const BMessenger& replyTo,
	uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->SetAdapterPairableAsync(fPath.String(), pairable, replyTo,
		replyWhat);
}


status_t
LocalDevice::StartDiscovery(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->StartDiscoveryAsync(fPath.String(), replyTo, replyWhat);
}


status_t
LocalDevice::StopDiscovery(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->StopDiscoveryAsync(fPath.String(), replyTo, replyWhat);
}


DiscoveryAgent*
LocalDevice::GetDiscoveryAgent() const
{
	DiscoveryAgent* agent = new DiscoveryAgent(fPath);
	agent->SetLocalDeviceOwner(const_cast<LocalDevice*>(this));
	return agent;
}


// #pragma mark - air/OS API


/*!	Re-reads the adapter's properties from BlueZ, so the synchronous getters
	of air/OS's API answer the current state as Haiku's did by asking the
	controller.
*/
void
LocalDevice::_Refresh()
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL || fPath.IsEmpty())
		return;

	BMessage info;
	if (backend->GetAdapterInfo(fPath.String(), &info) != B_OK)
		return;

	const char* name;
	if (info.FindString("alias", &name) == B_OK
		|| info.FindString("name", &name) == B_OK)
		fName = name;

	uint32 classRecord;
	if (info.FindUInt32("class", &classRecord) == B_OK)
		fDeviceClass.SetRecord(classRecord);

	info.FindBool("powered", &fPowered);
	info.FindBool("discoverable", &fDiscoverable);
	info.FindBool("discovering", &fDiscovering);
	info.FindBool("pairable", &fPairable);
}


uint32
LocalDevice::GetLocalDeviceCount()
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return 0;

	BMessage adapters;
	int32 count = 0;
	if (backend->GetAdapters(&adapters) != B_OK
		|| adapters.FindInt32("adapter_count", &count) != B_OK
		|| count < 0)
		return 0;

	return (uint32)count;
}


LocalDevice*
LocalDevice::GetLocalDevice()
{
	return GetLocalDevice((hci_id)0);
}


/*!	The hci_id of an adapter is its index in BlueZ's adapter list. The
	caller owns the returned device.
*/
LocalDevice*
LocalDevice::GetLocalDevice(const hci_id hid)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL || hid < 0)
		return NULL;

	BMessage adapters;
	if (backend->GetAdapters(&adapters) != B_OK)
		return NULL;

	BString name;
	name << "adapter_" << hid;

	BMessage info;
	if (adapters.FindMessage(name.String(), &info) != B_OK)
		return NULL;

	LocalDevice* device = new(std::nothrow) LocalDevice(info);
	if (device != NULL)
		device->fHid = hid;
	return device;
}


LocalDevice*
LocalDevice::GetLocalDevice(const bdaddr_t bdaddr)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return NULL;

	BMessage adapters;
	int32 count = 0;
	if (backend->GetAdapters(&adapters) != B_OK
		|| adapters.FindInt32("adapter_count", &count) != B_OK)
		return NULL;

	for (int32 i = 0; i < count; i++) {
		BString name;
		name << "adapter_" << i;

		BMessage info;
		const char* address;
		if (adapters.FindMessage(name.String(), &info) != B_OK
			|| info.FindString("address", &address) != B_OK)
			continue;

		if (bdaddrUtils::Compare(bdaddrUtils::FromString(address), bdaddr)) {
			LocalDevice* device = new(std::nothrow) LocalDevice(info);
			if (device != NULL)
				device->fHid = i;
			return device;
		}
	}

	return NULL;
}


BString
LocalDevice::GetFriendlyName()
{
	_Refresh();
	return fName;
}


BString
LocalDevice::GetFriendlyName() const
{
	return fName;
}


status_t
LocalDevice::SetFriendlyName(BString& name)
{
	// TODO(linux backend): org.bluez.Adapter1.Alias is writable, but
	// BlueZBackend has no setter for it yet.
	return B_NOT_SUPPORTED;
}


DeviceClass
LocalDevice::GetDeviceClass()
{
	_Refresh();
	return fDeviceClass;
}


DeviceClass
LocalDevice::GetDeviceClass() const
{
	return fDeviceClass;
}


status_t
LocalDevice::SetDeviceClass(DeviceClass deviceClass)
{
	// BlueZ derives the class of device from /etc/bluetooth/main.conf and
	// the registered profiles; org.bluez.Adapter1.Class is read-only.
	return B_NOT_SUPPORTED;
}


/*!	 mode is the HCI scan enable value: bit 0 inquiry scan (discoverable),
	bit 1 page scan (connectable). BlueZ keeps the adapter connectable while
	it is powered, so only the inquiry scan bit is applied.
*/
status_t
LocalDevice::SetDiscoverable(int mode)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	bool discoverable = (mode & 1) != 0;
	status_t status = backend->SetAdapterDiscoverable(fPath.String(),
		discoverable);
	if (status == B_OK)
		fDiscoverable = discoverable;
	return status;
}


status_t
LocalDevice::SetAuthentication(bool authentication)
{
	// TODO(linux backend): BlueZ always pairs with Secure Simple Pairing;
	// legacy authentication enable is only reachable through the mgmt API
	// (MGMT_OP_SET_LINK_SECURITY), not over D-Bus.
	return B_NOT_SUPPORTED;
}


int
LocalDevice::GetDiscoverable()
{
	_Refresh();

	int mode = 0;
	if (fDiscoverable)
		mode |= 1;
	if (fPowered)
		mode |= 2;
	return mode;
}


bdaddr_t
LocalDevice::GetBluetoothAddress()
{
	return fBdaddr;
}


BString
LocalDevice::GetProperty(const char* property)
{
	// TODO(linux backend): Haiku reads HCI version, manufacturer and buffer
	// sizes from the controller; BlueZ's D-Bus API does not show them (the
	// mgmt API's Read Controller Information does).
	return BString();
}


status_t
LocalDevice::GetProperty(const char* property, uint32* value)
{
	if (property == NULL || value == NULL)
		return B_BAD_VALUE;

	if (strcmp(property, "class") == 0) {
		_Refresh();
		*value = fDeviceClass.Record();
		return B_OK;
	}

	// TODO(linux backend): see GetProperty(const char*).
	return B_ERROR;
}


hci_id
LocalDevice::ID() const
{
	return fHid;
}


} // namespace Bluetooth
