/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _MPRIS_SERVICE_H
#define _MPRIS_SERVICE_H

#include <atomic>
#include <Messenger.h>
#include <OS.h>
#include <systemd/sd-bus.h>

class BMessage;

class MprisService {
public:
								MprisService(const BMessenger& target);
								~MprisService();

			status_t			Start();
			void				NotifyPropertiesChanged();

private:
	static	int32				_Worker(void* data);
			int32				_Run();
			status_t			_SendCommand(sd_bus_message* message,
									const char* action,
									const char* signature = NULL);
			status_t			_SendProperty(BMessage& request);
	static	int					_ReplyForCommand(sd_bus_message*, void*,
									const char*, const char*);
			status_t			_Query(BMessage& reply);
			void				_EmitPropertiesChanged();

	static	int					_Raise(sd_bus_message*, void*, sd_bus_error*);
	static	int					_Quit(sd_bus_message*, void*, sd_bus_error*);
	static	int					_Play(sd_bus_message*, void*, sd_bus_error*);
	static	int					_Pause(sd_bus_message*, void*, sd_bus_error*);
	static	int					_PlayPause(sd_bus_message*, void*, sd_bus_error*);
	static	int					_Stop(sd_bus_message*, void*, sd_bus_error*);
	static	int					_Next(sd_bus_message*, void*, sd_bus_error*);
	static	int					_Previous(sd_bus_message*, void*, sd_bus_error*);
	static	int					_Seek(sd_bus_message*, void*, sd_bus_error*);
	static	int					_SetPosition(sd_bus_message*, void*, sd_bus_error*);
	static	int					_OpenUri(sd_bus_message*, void*, sd_bus_error*);
	static	int					_GetRootProperty(sd_bus*, const char*,
									const char*, const char*, sd_bus_message*,
									void*, sd_bus_error*);
	static	int					_GetPlayerProperty(sd_bus*, const char*,
									const char*, const char*, sd_bus_message*,
									void*, sd_bus_error*);
	static	int					_SetPlayerProperty(sd_bus*, const char*,
									const char*, const char*, sd_bus_message*,
									void*, sd_bus_error*);

			BMessenger			fTarget;
			std::atomic<bool>	fRunning;
			std::atomic<status_t>	fStartupStatus;
			thread_id			fThread;
			port_id				fWakePort;
			sem_id				fStartupSem;
			sd_bus*				fBus;
};

#endif
