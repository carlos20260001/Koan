/*
 * Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _NOTIFICATION_SERVER_H
#define _NOTIFICATION_SERVER_H


#include <Server.h>
#include <systemd/sd-bus.h>

#include <atomic>
#include <map>
#include <OS.h>
#include <String.h>
#include <Locker.h>

class NotificationWindow;


class NotificationServer : public BServer {
public:
								NotificationServer(status_t& error);
	virtual						~NotificationServer();

	virtual	void				ReadyToRun();
	virtual	void				MessageReceived(BMessage* message);

	virtual	status_t			GetSupportedSuites(BMessage* msg);
	virtual	BHandler*			ResolveSpecifier(BMessage* msg, int32 index,
									BMessage* spec, int32 form,
									const char* prop);

private:
	static	int32				_DBUSWorker(void* data);
			int32				_RunDBus();
	static	int					_Notify(sd_bus_message* message, void* data,
									sd_bus_error* error);
	static	int					_CloseNotification(sd_bus_message* message,
									void* data, sd_bus_error* error);
	static	int					_GetCapabilities(sd_bus_message* message,
									void* data, sd_bus_error* error);
	static	int					_GetServerInformation(sd_bus_message* message,
									void* data, sd_bus_error* error);

			NotificationWindow*	fWindow;
			std::atomic<bool>	fRunning;
			thread_id			fBusThread;
			sd_bus*				fBus;
			port_id				fClosedEventPort;
			BLocker				fNotificationLock;
			std::map<uint32, BString> fNotificationMessages;
			uint32				fNextNotificationID;
};


#endif	// _NOTIFICATION_SERVER_H
