/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <bluetooth/RemoteDevice.h>
#include <bluetooth/LocalDevice.h>
#include <bluetooth/bdaddrUtils.h>

#include <Message.h>

#include <new>
#include <string.h>

#include "BlueZBackend.h"


namespace Bluetooth {


status_t
RemoteDevice::FetchAllAsync(const BMessenger& replyTo, uint32 replyWhat)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->GetDevicesAsync(replyTo, replyWhat);
}


status_t
RemoteDevice::DevicesFromMessage(const BMessage& devicesReply,
	BObjectList<RemoteDevice, true>& outList)
{
	int32 count = 0;
	if (devicesReply.FindInt32("device_count", &count) != B_OK)
		return B_BAD_VALUE;

	for (int32 i = 0; i < count; i++) {
		BString name;
		name << "device_" << i;

		BMessage info;
		if (devicesReply.FindMessage(name.String(), &info) != B_OK)
			continue;

		outList.AddItem(new RemoteDevice(info));
	}

	return B_OK;
}


RemoteDevice::RemoteDevice(const BMessage& deviceInfo)
	:
	BluetoothDevice(),
	fDiscovererLocalDevice(NULL),
	fPageRepetitionMode(0),
	fScanPeriodMode(0),
	fScanMode(0),
	fClockOffset(0),
	fRSSI(0),
	fFriendlyNameIsComplete(false),
	fPaired(false),
	fConnected(false),
	fTrusted(false),
	fBlocked(false)
{
	_SetFromInfo(deviceInfo);
}


RemoteDevice::RemoteDevice(const BString& address)
	:
	BluetoothDevice(),
	fDiscovererLocalDevice(NULL),
	fPageRepetitionMode(0),
	fScanPeriodMode(0),
	fScanMode(0),
	fClockOffset(0),
	fRSSI(0),
	fFriendlyNameIsComplete(false),
	fAddress(address),
	fPaired(false),
	fConnected(false),
	fTrusted(false),
	fBlocked(false)
{
	fBdaddr = bdaddrUtils::FromString(address.String());
}


RemoteDevice::RemoteDevice(const bdaddr_t address, uint8 record[3])
	:
	BluetoothDevice(),
	fDiscovererLocalDevice(NULL),
	fPageRepetitionMode(0),
	fScanPeriodMode(0),
	fScanMode(0),
	fClockOffset(0),
	fRSSI(0),
	fFriendlyNameIsComplete(false),
	fPaired(false),
	fConnected(false),
	fTrusted(false),
	fBlocked(false)
{
	fBdaddr = address;
	fAddress = bdaddrUtils::ToString(address);
	fDeviceClass.SetRecord(record);
}


void
RemoteDevice::_SetFromInfo(const BMessage& deviceInfo)
{
	const char* path;
	if (deviceInfo.FindString("path", &path) == B_OK)
		fPath = path;

	const char* adapter;
	if (deviceInfo.FindString("adapter", &adapter) == B_OK)
		fAdapterPath = adapter;

	const char* address;
	if (deviceInfo.FindString("address", &address) == B_OK) {
		fAddress = address;
		fBdaddr = bdaddrUtils::FromString(address);
	}

	const char* name;
	if (deviceInfo.FindString("name", &name) == B_OK)
		fName = name;

	const char* alias;
	if (deviceInfo.FindString("alias", &alias) == B_OK)
		fAlias = alias;

	uint32 classRecord;
	if (deviceInfo.FindUInt32("class", &classRecord) == B_OK)
		fDeviceClass.SetRecord(classRecord);

	int16 rssi;
	if (deviceInfo.FindInt16("rssi", &rssi) == B_OK)
		fRSSI = (int8)rssi;

	deviceInfo.FindBool("paired", &fPaired);
	deviceInfo.FindBool("connected", &fConnected);
	deviceInfo.FindBool("trusted", &fTrusted);
	deviceInfo.FindBool("blocked", &fBlocked);

	if (!fAlias.IsEmpty() || !fName.IsEmpty()) {
		fFriendlyName = !fAlias.IsEmpty() ? fAlias : fName;
		fFriendlyNameIsComplete = true;
	}
}


/*!	Finds the BlueZ object of a device that was made from its address
	(air/OS's constructors), and refreshes the cached properties.
*/
status_t
RemoteDevice::_ResolvePath()
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	if (!fPath.IsEmpty()) {
		BMessage info;
		status_t status = backend->GetDeviceInfo(fPath.String(), &info);
		if (status == B_OK)
			_SetFromInfo(info);
		return status;
	}

	BMessage devices;
	int32 count = 0;
	if (backend->GetDevices(&devices) != B_OK
		|| devices.FindInt32("device_count", &count) != B_OK)
		return B_ERROR;

	for (int32 i = 0; i < count; i++) {
		BString itemName;
		itemName << "device_" << i;

		BMessage info;
		const char* address;
		if (devices.FindMessage(itemName.String(), &info) != B_OK
			|| info.FindString("address", &address) != B_OK)
			continue;

		if (!bdaddrUtils::Compare(bdaddrUtils::FromString(address), fBdaddr))
			continue;

		_SetFromInfo(info);
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


RemoteDevice::~RemoteDevice()
{
}


BString
RemoteDevice::GetFriendlyName() const
{
	return _FriendlyName();
}


BString
RemoteDevice::_FriendlyName() const
{
	if (!fAlias.IsEmpty())
		return fAlias;
	if (!fName.IsEmpty())
		return fName;
	return fAddress;
}


bool
RemoteDevice::Equals(RemoteDevice* other) const
{
	if (other == NULL)
		return false;
	if (!fPath.IsEmpty() && !other->fPath.IsEmpty())
		return fPath == other->fPath;
	return bdaddrUtils::Compare(fBdaddr, other->fBdaddr);
}


status_t
RemoteDevice::Connect(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->ConnectDeviceAsync(fPath.String(), replyTo, replyWhat);
}


status_t
RemoteDevice::Disconnect(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->DisconnectDeviceAsync(fPath.String(), replyTo, replyWhat);
}


status_t
RemoteDevice::Pair(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->PairDeviceAsync(fPath.String(), replyTo, replyWhat);
}


status_t
RemoteDevice::Unpair(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->UnpairDeviceAsync(fPath.String(), replyTo, replyWhat);
}


status_t
RemoteDevice::SetTrusted(bool trusted, const BMessenger& replyTo,
	uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->SetDeviceTrustedAsync(fPath.String(), trusted, replyTo,
		replyWhat);
}


status_t
RemoteDevice::SetBlocked(bool blocked, const BMessenger& replyTo,
	uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->SetDeviceBlockedAsync(fPath.String(), blocked, replyTo,
		replyWhat);
}


// #pragma mark - air/OS API


BObjectList<RemoteDevice>
RemoteDevice::GetRemoteDevices(LocalDevice* localDevice)
{
	// The devices BlueZ knows on that adapter (paired or seen); the caller
	// owns them, as with Haiku.
	BObjectList<RemoteDevice> list;

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
		if (devices.FindMessage(itemName.String(), &info) != B_OK)
			continue;

		RemoteDevice* device = new(std::nothrow) RemoteDevice(info);
		if (device == NULL)
			break;

		if (localDevice != NULL && !localDevice->Path().IsEmpty()
			&& device->AdapterPath() != localDevice->Path()) {
			delete device;
			continue;
		}

		device->SetLocalDeviceOwner(localDevice);
		list.AddItem(device);
	}

	return list;
}


RemoteDevice*
RemoteDevice::ParseRemoteDevice(LocalDevice* localDevice, BMessage* message)
{
	if (message == NULL)
		return NULL;

	RemoteDevice* device = new(std::nothrow) RemoteDevice(*message);
	if (device != NULL)
		device->SetLocalDeviceOwner(localDevice);
	return device;
}


bool
RemoteDevice::IsTrustedDevice() const
{
	return fTrusted;
}


BString
RemoteDevice::GetFriendlyName(bool alwaysAsk)
{
	if (alwaysAsk || !fFriendlyNameIsComplete)
		_ResolvePath();

	return _FriendlyName();
}


BString
RemoteDevice::GetFriendlyName()
{
	// BlueZ asks for the remote name itself while it discovers or connects;
	// a known name is what asking again would return, so only a device
	// without one costs a D-Bus round trip (GUI code calls this).
	return GetFriendlyName(false);
}


BString
RemoteDevice::GetCachedFriendlyName()
{
	return fFriendlyName;
}


bdaddr_t
RemoteDevice::GetBluetoothAddress()
{
	return fBdaddr;
}


uint8
RemoteDevice::GetPageRepetitionMode()
{
	// HCI inquiry details; BlueZ pages the device itself.
	return fPageRepetitionMode;
}


uint16
RemoteDevice::GetClockOffset()
{
	return fClockOffset;
}


DeviceClass
RemoteDevice::GetDeviceClass()
{
	return fDeviceClass;
}


DeviceClass
RemoteDevice::GetDeviceClass() const
{
	return fDeviceClass;
}


status_t
RemoteDevice::Connect()
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	status_t status = _ResolvePath();
	if (status != B_OK)
		return status;

	return backend->ConnectDevice(fPath.String());
}


status_t
RemoteDevice::CancelConnection()
{
	// org.bluez.Device1.Disconnect also aborts a connection in progress.
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	status_t status = _ResolvePath();
	if (status != B_OK)
		return status;

	return backend->DisconnectDevice(fPath.String());
}


status_t
RemoteDevice::Disconnect(bool removeDevice)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	status_t status = _ResolvePath();
	if (status != B_OK)
		return status;

	status = backend->DisconnectDevice(fPath.String());
	if (removeDevice && status == B_OK)
		status = backend->UnpairDevice(fPath.String());
	return status;
}


RemoteDevice::ConnectionState
RemoteDevice::GetConnectionState()
{
	if (_ResolvePath() != B_OK)
		return RemoteDevice::DISCONNECTED;

	return fConnected ? RemoteDevice::CONNECTED : RemoteDevice::DISCONNECTED;
}


bool
RemoteDevice::IsEncrypted()
{
	// TODO(linux backend): org.bluez.Device1 does not show the link's
	// encryption (air/OS 8225f603e6 asks bluetooth_server). BlueZ encrypts
	// the link to a bonded device before it serves a profile, so a
	// connected, paired device is reported as encrypted.
	if (_ResolvePath() != B_OK)
		return false;

	return fConnected && fPaired;
}


BString
RemoteDevice::GetProperty(const char* property)
{
	// TODO(linux backend): Haiku answers LMP features and versions from the
	// controller; BlueZ's D-Bus API does not show them.
	return BString();
}


status_t
RemoteDevice::GetProperty(const char* property, uint32* value)
{
	if (property == NULL || value == NULL)
		return B_BAD_VALUE;

	if (strcmp(property, "class") == 0) {
		*value = fDeviceClass.Record();
		return B_OK;
	}

	// TODO(linux backend): see GetProperty(const char*).
	return B_ERROR;
}


LocalDevice*
RemoteDevice::GetLocalDeviceOwner()
{
	return fDiscovererLocalDevice;
}


void
RemoteDevice::SetLocalDeviceOwner(LocalDevice* ld)
{
	fDiscovererLocalDevice = ld;
}


} // namespace Bluetooth
