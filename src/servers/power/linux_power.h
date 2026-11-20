/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LINUX_POWER_H
#define _LINUX_POWER_H


/*!	The Linux side of the power daemon. On Haiku the daemon reads the ACPI
	lid, AC adapter and power button drivers and puts the machine to sleep
	itself; on Linux those are systemd-logind (lid and power key handling,
	sleep), UPower (power source, lid state) and power-profiles-daemon
	(the power saving profile), all over the system D-Bus (sd-bus). The
	policy in power_policy.cpp is the same on both.
*/


#include <set>

#include <OS.h>

#include <systemd/sd-bus.h>

#include "power_monitor.h"


class PowerPolicy;


/*!	Takes a logind "block" inhibitor lock for \a what ("handle-lid-switch",
	"handle-power-key"), so that logind leaves those events to the daemon.
	Returns the lock's file descriptor (the lock lasts until it is closed),
	or -1. */
int		linux_power_inhibit(const char* what, const char* why);

/*!	Suspends through logind and returns once the machine woke up again.
	Returns B_OK, or an error when it could not sleep. */
status_t linux_power_suspend();

/*!	Whether the lid is closed according to UPower: 1 closed, 0 open,
	-1 unknown (no lid, no UPower). */
int		linux_power_lid_closed();

/*!	Selects power-profiles-daemon's "power-saver" or "balanced" profile;
	nothing happens without the daemon. */
void	linux_power_set_power_saving(bool powerSaving);


/*!	Watches UPower's OnBattery and LidIsClosed properties and tells the
	policy about them, as the AC and lid monitors do on Haiku. While the
	machine has a lid, it holds logind's "handle-lid-switch" lock: the lid
	action is the Power preferences', not logind's. */
class LinuxPowerMonitor : public PowerMonitor {
public:
								LinuxPowerMonitor(PowerPolicy* policy);
	virtual						~LinuxPowerMonitor();

	virtual	void				HandleEvent(int fd);

	virtual	const std::set<int>&
								FDs() const { return fFDs; }

private:
	static	int					_PropertiesChanged(sd_bus_message* message,
									void* data, sd_bus_error* error);
			void				_Update();

			PowerPolicy*		fPolicy;
			sd_bus*				fBus;
			std::set<int>		fFDs;
			int					fLidInhibitor;
			bool				fChanged;
};


#endif // _LINUX_POWER_H
