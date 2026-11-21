/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef UPOWER_DRIVER_INTERFACE_H
#define UPOWER_DRIVER_INTERFACE_H


#include "DriverInterface.h"

#include <Locker.h>
#include <String.h>

#include <vector>


struct sd_bus;


/*!	The batteries as UPower (org.freedesktop.UPower on the system D-Bus)
	reports them; on Linux it takes the place of Haiku's ACPI battery
	driver. Capacities are given in mWh and rates in mW, like the ACPI
	driver's ACPI_BATTERY_UNIT_MW. */
class UPowerDriverInterface : public PowerStatusDriverInterface {
public:
								UPowerDriverInterface();
	virtual						~UPowerDriverInterface();

	virtual status_t			Connect();
	virtual status_t 			GetBatteryInfo(int32 index,
									battery_info* info);
	virtual status_t			GetExtendedBatteryInfo(int32 index,
									acpi_extended_battery_info* info);

	virtual int32				GetBatteryCount();

protected:
	virtual void				_WatchPowerStatus();

private:
	struct Battery {
		BString						path;
		battery_info				info;
		acpi_extended_battery_info	extended;
	};

			status_t			_Update();
			status_t			_ReadBattery(const char* path,
									Battery& battery);

			BLocker				fLock;
			sd_bus*				fBus;
			std::vector<Battery> fBatteries;
};


#endif	// UPOWER_DRIVER_INTERFACE_H
