/*
 * Copyright 2013, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2014, Rene Gollent, rene@gollent.com.
 * Copyright 2005, Nathan Whitehorn.
 *
 * Distributed under the terms of the MIT License.
 */


#include "ac_monitor.h"
#include "lid_monitor.h"
#include "power_button_monitor.h"
#include "power_policy.h"
#include "thermal_trip_monitor.h"

#include <Server.h>

#include <PathMonitor.h>

#include <map>


class PowerManagementDaemon : public BServer {
public:
								PowerManagementDaemon();
	virtual 					~PowerManagementDaemon();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_EventLoop();
	static	status_t			_EventLooper(void *arg);

			thread_id			fEventThread;
			PowerPolicy			fPolicy;
			PowerMonitor*		fPowerMonitors[4];
			uint32				fMonitorCount;

			bool				fQuitRequested;
};


int
main(void)
{
	new PowerManagementDaemon();
	be_app->Run();
	delete be_app;
	return 0;
}


PowerManagementDaemon::PowerManagementDaemon()
	:
	BServer("application/x-vnd.vos-powermanagement", false, NULL),
	fPolicy(this),
	fMonitorCount(0),
	fQuitRequested(false)
{
	PowerMonitor* powerButtonMonitor = new PowerButtonMonitor(&fPolicy);
	if (powerButtonMonitor->FDs().size() > 0)
		fPowerMonitors[fMonitorCount++] = powerButtonMonitor;
	else
		delete powerButtonMonitor;

	PowerMonitor* lidMonitor = new LidMonitor(&fPolicy);
	if (lidMonitor->FDs().size() > 0)
		fPowerMonitors[fMonitorCount++] = lidMonitor;
	else
		delete lidMonitor;

	PowerMonitor* acMonitor = new ACMonitor(&fPolicy);
	if (acMonitor->FDs().size() > 0)
		fPowerMonitors[fMonitorCount++] = acMonitor;
	else
		delete acMonitor;

	PowerMonitor* thermalMonitor = new ThermalTripMonitor;
	if (thermalMonitor->FDs().size() > 0)
		fPowerMonitors[fMonitorCount++] = thermalMonitor;
	else
		delete thermalMonitor;

	// the Power preferences change the settings file
	const char* settingsPath = PowerPolicy::SettingsPath();
	if (settingsPath != NULL) {
		BPrivate::BPathMonitor::StartWatching(settingsPath,
			B_WATCH_STAT | B_WATCH_FILES_ONLY, BMessenger(this));
	}

	fEventThread = spawn_thread(_EventLooper, "_power_daemon_event_loop_",
		B_NORMAL_PRIORITY, this);
	if (fEventThread < B_OK)
		return;
	if (resume_thread(fEventThread) < B_OK) {
		kill_thread(fEventThread);
		fEventThread = -1;
		return;
	}
}


PowerManagementDaemon::~PowerManagementDaemon()
{
	fQuitRequested = true;
	for (uint32 i = 0; i < fMonitorCount; i++)
		delete fPowerMonitors[i];
	status_t status;
	wait_for_thread(fEventThread, &status);
}


void
PowerManagementDaemon::MessageReceived(BMessage* message)
{
	if (message->what == B_PATH_MONITOR) {
		fPolicy.LoadSettings();
		return;
	}

	BServer::MessageReceived(message);
}


status_t
PowerManagementDaemon::_EventLooper(void* arg)
{
	PowerManagementDaemon* self = (PowerManagementDaemon*)arg;
	self->_EventLoop();
	return B_OK;
}


void
PowerManagementDaemon::_EventLoop()
{
	if (fMonitorCount == 0)
		return;

	std::map<int, PowerMonitor*> descriptorMap;

	size_t fdCount = 0;
	for (uint32 i = 0; i < fMonitorCount; i++)
		fdCount += fPowerMonitors[i]->FDs().size();

	object_wait_info info[fdCount];
	uint32 index = 0;
	for (uint32 i = 0; i < fMonitorCount; i++) {
		const std::set<int>& fds = fPowerMonitors[i]->FDs();
		for (std::set<int>::iterator it = fds.begin(); it != fds.end(); ++it) {
			info[index].object = *it;
			info[index].type = B_OBJECT_TYPE_FD;
			info[index].events = B_EVENT_READ;
			descriptorMap[*it] = fPowerMonitors[i];
			++index;
		}
	}
	while (!fQuitRequested) {
		bigtime_t timeout = fPolicy.PollInterval();
		ssize_t count = timeout == B_INFINITE_TIMEOUT
			? wait_for_objects(info, fdCount)
			: wait_for_objects_etc(info, fdCount, B_RELATIVE_TIMEOUT, timeout);
		if (count > 0) {
			// handle events and reset events
			for (uint32 i = 0; i < fdCount; i++) {
				if (info[i].events & B_EVENT_READ)
					descriptorMap[info[i].object]->HandleEvent(info[i].object);
				else
					info[i].events = B_EVENT_READ;
			}
		} else {
			for (uint32 i = 0; i < fdCount; i++)
				info[i].events = B_EVENT_READ;
		}
		fPolicy.Poll();
	}
}
