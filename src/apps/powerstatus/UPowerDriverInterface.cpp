/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "UPowerDriverInterface.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <Autolock.h>

#include <systemd/sd-bus.h>


static const char* kUPowerService = "org.freedesktop.UPower";
static const char* kUPowerPath = "/org/freedesktop/UPower";
static const char* kUPowerInterface = "org.freedesktop.UPower";
static const char* kDeviceInterface = "org.freedesktop.UPower.Device";

static const bigtime_t kUpdateInterval = 2000000;
	// every two seconds, like the ACPI interface

// org.freedesktop.UPower.Device "Type"
static const uint32 kDeviceTypeBattery = 2;

// org.freedesktop.UPower.Device "State"
enum {
	kStateUnknown = 0,
	kStateCharging,
	kStateDischarging,
	kStateEmpty,
	kStateFullyCharged,
	kStatePendingCharge,
	kStatePendingDischarge
};

// org.freedesktop.UPower.Device "Technology"
static const char* kTechnologyNames[] = {
	"",
	"Li-ion",
	"Li-poly",
	"LiFePO4",
	"Lead acid",
	"NiCd",
	"NiMH"
};


namespace {

struct DeviceProperties {
	DeviceProperties()
		:
		type(0),
		state(kStateUnknown),
		technology(0),
		powerSupply(false),
		isPresent(true),
		isRechargeable(true),
		energy(0),
		energyFull(0),
		energyFullDesign(0),
		energyRate(0),
		voltage(0),
		percentage(0),
		timeToEmpty(0),
		timeToFull(0),
		chargeCycles(-1)
	{
	}

	uint32		type;
	uint32		state;
	uint32		technology;
	bool		powerSupply;
	bool		isPresent;
	bool		isRechargeable;
	double		energy;
	double		energyFull;
	double		energyFullDesign;
	double		energyRate;
	double		voltage;
	double		percentage;
	int64		timeToEmpty;
	int64		timeToFull;
	int32		chargeCycles;
	BString		model;
	BString		vendor;
	BString		serial;
};


/*!	Reads one variant value of a GetAll() reply into \a properties, by
	name. Unknown names and unexpected types are skipped. */
static int
read_property(sd_bus_message* reply, const char* name,
	DeviceProperties& properties)
{
	char type;
	const char* contents;
	int result = sd_bus_message_peek_type(reply, &type, &contents);
	if (result < 0)
		return result;
	if (type != SD_BUS_TYPE_VARIANT || contents == NULL)
		return sd_bus_message_skip(reply, NULL);

	switch (contents[0]) {
		case SD_BUS_TYPE_DOUBLE:
		{
			double value;
			result = sd_bus_message_read(reply, "v", "d", &value);
			if (result < 0)
				return result;
			if (strcmp(name, "Energy") == 0)
				properties.energy = value;
			else if (strcmp(name, "EnergyFull") == 0)
				properties.energyFull = value;
			else if (strcmp(name, "EnergyFullDesign") == 0)
				properties.energyFullDesign = value;
			else if (strcmp(name, "EnergyRate") == 0)
				properties.energyRate = value;
			else if (strcmp(name, "Voltage") == 0)
				properties.voltage = value;
			else if (strcmp(name, "Percentage") == 0)
				properties.percentage = value;
			return result;
		}

		case SD_BUS_TYPE_UINT32:
		{
			uint32_t value;
			result = sd_bus_message_read(reply, "v", "u", &value);
			if (result < 0)
				return result;
			if (strcmp(name, "Type") == 0)
				properties.type = value;
			else if (strcmp(name, "State") == 0)
				properties.state = value;
			else if (strcmp(name, "Technology") == 0)
				properties.technology = value;
			return result;
		}

		case SD_BUS_TYPE_INT32:
		{
			int32_t value;
			result = sd_bus_message_read(reply, "v", "i", &value);
			if (result >= 0 && strcmp(name, "ChargeCycles") == 0)
				properties.chargeCycles = value;
			return result;
		}

		case SD_BUS_TYPE_INT64:
		{
			int64_t value;
			result = sd_bus_message_read(reply, "v", "x", &value);
			if (result < 0)
				return result;
			if (strcmp(name, "TimeToEmpty") == 0)
				properties.timeToEmpty = value;
			else if (strcmp(name, "TimeToFull") == 0)
				properties.timeToFull = value;
			return result;
		}

		case SD_BUS_TYPE_BOOLEAN:
		{
			int value;
			result = sd_bus_message_read(reply, "v", "b", &value);
			if (result < 0)
				return result;
			if (strcmp(name, "PowerSupply") == 0)
				properties.powerSupply = value != 0;
			else if (strcmp(name, "IsPresent") == 0)
				properties.isPresent = value != 0;
			else if (strcmp(name, "IsRechargeable") == 0)
				properties.isRechargeable = value != 0;
			return result;
		}

		case SD_BUS_TYPE_STRING:
		{
			const char* value;
			result = sd_bus_message_read(reply, "v", "s", &value);
			if (result < 0)
				return result;
			if (strcmp(name, "Model") == 0)
				properties.model = value;
			else if (strcmp(name, "Vendor") == 0)
				properties.vendor = value;
			else if (strcmp(name, "Serial") == 0)
				properties.serial = value;
			return result;
		}

		default:
			return sd_bus_message_skip(reply, "v");
	}
}


static int
get_all_properties(sd_bus* bus, const char* path,
	DeviceProperties& properties)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	int result = sd_bus_call_method(bus, kUPowerService, path,
		"org.freedesktop.DBus.Properties", "GetAll", &error, &reply, "s",
		kDeviceInterface);
	sd_bus_error_free(&error);
	if (result < 0)
		return result;

	result = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
	while (result >= 0) {
		result = sd_bus_message_enter_container(reply,
			SD_BUS_TYPE_DICT_ENTRY, "sv");
		if (result <= 0)
			break;

		const char* name;
		result = sd_bus_message_read(reply, "s", &name);
		if (result >= 0)
			result = read_property(reply, name, properties);
		if (result >= 0)
			result = sd_bus_message_exit_container(reply);
	}

	sd_bus_message_unref(reply);
	return result;
}


static int32
to_milli(double value)
{
	if (value <= 0)
		return 0;
	return (int32)(value * 1000 + 0.5);
}

}	// namespace


// #pragma mark -


UPowerDriverInterface::UPowerDriverInterface()
	:
	fLock("upower interface"),
	fBus(NULL)
{
}


UPowerDriverInterface::~UPowerDriverInterface()
{
	Disconnect();
	sd_bus_flush_close_unref(fBus);
}


status_t
UPowerDriverInterface::Connect()
{
	BAutolock _(fLock);
	if (fBus == NULL) {
		int result = sd_bus_open_system(&fBus);
		if (result < 0) {
			fBus = NULL;
			return B_ERROR;
		}
	}

	status_t status = _Update();
	if (status == B_OK && fBatteries.empty())
		status = B_ENTRY_NOT_FOUND;
	return status;
}


status_t
UPowerDriverInterface::GetBatteryInfo(int32 index, battery_info* info)
{
	BAutolock _(fLock);
	if (index < 0 || index >= (int32)fBatteries.size())
		return B_BAD_INDEX;

	*info = fBatteries[index].info;
	return B_OK;
}


status_t
UPowerDriverInterface::GetExtendedBatteryInfo(int32 index,
	acpi_extended_battery_info* info)
{
	BAutolock _(fLock);
	if (index < 0 || index >= (int32)fBatteries.size())
		return B_BAD_INDEX;

	*info = fBatteries[index].extended;
	return B_OK;
}


int32
UPowerDriverInterface::GetBatteryCount()
{
	BAutolock _(fLock);
	return fBatteries.size();
}


void
UPowerDriverInterface::_WatchPowerStatus()
{
	while (atomic_get(&fIsWatching) > 0) {
		fLock.Lock();
		_Update();
		fLock.Unlock();

		Broadcast(kMsgUpdate);
		acquire_sem_etc(fWaitSem, 1, B_RELATIVE_TIMEOUT, kUpdateInterval);
	}
}


/*!	Lists UPower's devices again (batteries come and go) and reads the
	ones that power the system. Must be called with fLock held. */
status_t
UPowerDriverInterface::_Update()
{
	if (fBus == NULL)
		return B_NO_INIT;

	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	int result = sd_bus_call_method(fBus, kUPowerService, kUPowerPath,
		kUPowerInterface, "EnumerateDevices", &error, &reply, NULL);
	sd_bus_error_free(&error);
	if (result < 0)
		return B_ERROR;

	std::vector<Battery> batteries;
	result = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "o");
	while (result >= 0) {
		const char* path;
		result = sd_bus_message_read(reply, "o", &path);
		if (result <= 0)
			break;

		Battery battery;
		if (_ReadBattery(path, battery) == B_OK)
			batteries.push_back(battery);
	}
	sd_bus_message_unref(reply);

	if (result < 0)
		return B_ERROR;

	fBatteries.swap(batteries);
	return B_OK;
}


status_t
UPowerDriverInterface::_ReadBattery(const char* path, Battery& battery)
{
	DeviceProperties properties;
	if (get_all_properties(fBus, path, properties) < 0)
		return B_ERROR;

	// Only the batteries that power the computer, not those of a mouse
	// or a phone.
	if (properties.type != kDeviceTypeBattery || !properties.powerSupply)
		return B_BAD_TYPE;

	battery.path = path;

	battery_info& info = battery.info;
	acpi_extended_battery_info& extended = battery.extended;
	memset(&info, 0, sizeof(info));
	memset(&extended, 0, sizeof(extended));

	switch (properties.state) {
		case kStateCharging:
			info.state = BATTERY_CHARGING;
			break;
		case kStateDischarging:
		case kStateEmpty:
			info.state = BATTERY_DISCHARGING;
			break;
		case kStateFullyCharged:
		case kStatePendingCharge:
		case kStatePendingDischarge:
			info.state = BATTERY_NOT_CHARGING;
			break;
		default:
			info.state = 0;
			break;
	}

	if (!properties.isPresent) {
		// What the ACPI driver reports for an empty battery slot.
		info.state = BATTERY_CRITICAL_STATE;
		info.time_left = -1;
		return B_OK;
	}

	if (properties.energyFull > 0) {
		info.capacity = to_milli(properties.energy);
		info.full_capacity = to_milli(properties.energyFull);
	} else if (properties.energyFullDesign > 0) {
		info.capacity = to_milli(properties.energy);
		info.full_capacity = to_milli(properties.energyFullDesign);
	} else {
		// Without energy figures, the percentage still gives the charge.
		info.capacity = (int32)(properties.percentage * 10 + 0.5);
		info.full_capacity = 1000;
	}
	info.current_rate = to_milli(properties.energyRate);

	info.time_left = -1;
	if (properties.state == kStateDischarging && properties.timeToEmpty > 0)
		info.time_left = properties.timeToEmpty;
	else if (properties.state == kStateCharging && properties.timeToFull > 0)
		info.time_left = properties.timeToFull;

	extended.power_unit = ACPI_BATTERY_UNIT_MW;
	extended.design_capacity = to_milli(properties.energyFullDesign);
	extended.last_full_charge = info.full_capacity;
	extended.technology = properties.isRechargeable ? 1 : 0;
	// UPower reports only the present voltage, not the design voltage.
	extended.design_voltage = to_milli(properties.voltage);
	extended.revision = ACPI_BATTERY_REVISION_0;
	extended.cycles = properties.chargeCycles > 0
		? properties.chargeCycles : 0;

	strlcpy(extended.model_number, properties.model.String(),
		sizeof(extended.model_number));
	strlcpy(extended.serial_number, properties.serial.String(),
		sizeof(extended.serial_number));
	if (properties.technology
			< sizeof(kTechnologyNames) / sizeof(kTechnologyNames[0])) {
		strlcpy(extended.type, kTechnologyNames[properties.technology],
			sizeof(extended.type));
	}
	strlcpy(extended.oem_info, properties.vendor.String(),
		sizeof(extended.oem_info));

	return B_OK;
}
