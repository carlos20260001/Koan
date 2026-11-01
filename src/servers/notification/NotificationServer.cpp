/*
 * Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 */


#include "NotificationServer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <Alert.h>
#include <Autolock.h>
#include <Beep.h>
#include <Message.h>
#include <Messenger.h>
#include <Notification.h>
#include <Notifications.h>
#include <OS.h>
#include <SupportDefs.h>
#include <PropertyInfo.h>
#include <Roster.h>
#include <systemd/sd-bus.h>

#include "NotificationInterface.h"
#include "NotificationWindow.h"


struct NotificationClosedEvent {
	uint32 id;
	uint32 reason;
};


const char* kSoundNames[] = {
	"Notification information",
	"Notification important",
	"Notification error",
	"Notification progress",
	NULL
};


NotificationServer::NotificationServer(status_t& error)
	:
	BServer(kNotificationServerSignature, true, &error),
	fWindow(NULL),
	fRunning(false),
	fBusThread(-1),
	fBus(NULL),
	fClosedEventPort(-1),
	fNextNotificationID(1)
{
}


NotificationServer::~NotificationServer()
{
	fRunning.store(false);
	if (fBusThread >= 0) {
		status_t result;
		wait_for_thread(fBusThread, &result);
	}

	if (fBus != NULL)
		sd_bus_flush_close_unref(fBus);
	if (fClosedEventPort >= 0)
		delete_port(fClosedEventPort);
}


void
NotificationServer::ReadyToRun()
{
	fWindow = new NotificationWindow();
	if (fWindow == NULL)
		fprintf(stderr, "notification_server: could not create notification window\n");

	fClosedEventPort = create_port(64, "notification_dbus_events");
	if (fClosedEventPort < B_OK) {
		fprintf(stderr, "notification_server: could not create D-Bus event port "
			"(%" B_PRId32 ")\n", fClosedEventPort);
		fClosedEventPort = -1;
	}

	fRunning.store(true);
	fBusThread = spawn_thread(_DBUSWorker, "notification-dbus",
		B_NORMAL_PRIORITY, this);
	if (fBusThread < B_OK) {
		fprintf(stderr, "notification_server: could not start D-Bus worker "
			"(%" B_PRId32 ")\n", fBusThread);
		fRunning.store(false);
		return;
	}

	status_t result = resume_thread(fBusThread);
	if (result != B_OK) {
		fprintf(stderr, "notification_server: could not resume D-Bus worker "
			"(%" B_PRId32 ")\n", result);
		fRunning.store(false);
		kill_thread(fBusThread);
		wait_for_thread(fBusThread, &result);
		fBusThread = -1;
	}
}


void
NotificationServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kNotificationCenterToggle:
			if (fWindow != NULL) {
				if (fWindow->IsActive() && !fWindow->IsHidden())
					fWindow->Hide();
				else {
					fWindow->Show();
					fWindow->Activate();
				}
			}
			break;

		case kNotificationClose:
		{
			BMessage reply(B_REPLY);
			status_t status = B_ERROR;
			if (fWindow != NULL) {
				status = BMessenger(fWindow).SendMessage(message, &reply,
					5000000LL, 5000000LL);
			}
			if (status != B_OK) {
				reply.AddInt32("error", status);
				reply.what = B_MESSAGE_NOT_UNDERSTOOD;
			}
			message->SendReply(&reply);
			break;
		}

		case kNotificationClosed:
		{
			const char* messageID = NULL;
			int32 reason = 1;
			if (message->FindString("message_id", &messageID) != B_OK
				|| messageID == NULL) {
				break;
			}
			message->FindInt32("reason", &reason);

			NotificationClosedEvent event = { 0, (uint32)reason };
			fNotificationLock.Lock();
			for (std::map<uint32, BString>::iterator it
					= fNotificationMessages.begin();
				it != fNotificationMessages.end(); ++it) {
				if (it->second == messageID) {
					event.id = it->first;
					fNotificationMessages.erase(it);
					break;
				}
			}
			fNotificationLock.Unlock();

			if (event.id != 0 && fClosedEventPort >= B_OK) {
				status_t status = write_port_etc(fClosedEventPort, 0, &event,
					sizeof(event), B_RELATIVE_TIMEOUT, 0);
				if (status != B_OK) {
					fprintf(stderr, "notification_server: could not queue "
						"NotificationClosed signal (%" B_PRId32 ")\n", status);
				}
			}
			break;
		}

		case kNotificationMessage:
		{
			if (fWindow == NULL) {
				BMessage reply(B_MESSAGE_NOT_UNDERSTOOD);
				reply.AddInt32("error", B_NO_INIT);
				message->SendReply(&reply);
				return;
			}

			// Emit a sound for this event
			// Progress notifications only emit a sound if the visible percentage changed
			int32 type = 0;
			if (message->FindInt32("_type", &type) == B_OK) {
				if (type < (int32)(sizeof(kSoundNames) / sizeof(const char*))
					&& type != B_PROGRESS_NOTIFICATION)
					system_beep(kSoundNames[type]);
			}

			BMessage reply(B_REPLY);
			status_t status = BMessenger(fWindow).SendMessage(message, &reply,
				5000000LL, 5000000LL);
			if (status != B_OK) {
				reply.what = B_MESSAGE_NOT_UNDERSTOOD;
				reply.AddInt32("error", status);
			}
			message->SendReply(&reply);
			break;
		}
		default:
			BApplication::MessageReceived(message);
	}
}


int32
NotificationServer::_DBUSWorker(void* data)
{
	return ((NotificationServer*)data)->_RunDBus();
}


int32
NotificationServer::_RunDBus()
{
	int result = sd_bus_open_user(&fBus);
	if (result < 0) {
		fprintf(stderr, "notification_server: sd_bus_open_user: %s\n",
			strerror(-result));
		return result;
	}

	static const sd_bus_vtable vtable[] = {
		SD_BUS_VTABLE_START(0),
		SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", _Notify,
			SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("CloseNotification", "u", "", _CloseNotification,
			SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("GetCapabilities", "", "as", _GetCapabilities,
			SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("GetServerInformation", "", "ssss",
			_GetServerInformation, SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
		SD_BUS_VTABLE_END
	};

	result = sd_bus_add_object_vtable(fBus, NULL, NOTIFICATION_PATH,
		NOTIFICATION_INTERFACE, vtable, this);
	if (result < 0) {
		fprintf(stderr, "notification_server: registering notification "
			"interface failed: %s\n", strerror(-result));
		return result;
	}

	result = sd_bus_request_name(fBus, NOTIFICATION_SERVICE, 0);
	if (result <= 0) {
		fprintf(stderr, "notification_server: could not own D-Bus name "
			"%s: %s\n", NOTIFICATION_SERVICE,
			result < 0 ? strerror(-result) : "already owned");
		return result < 0 ? result : -EEXIST;
	}

	while (fRunning.load()) {
		if (fClosedEventPort >= B_OK) {
			NotificationClosedEvent event;
			int32 code;
			while (read_port_etc(fClosedEventPort, &code, &event,
					sizeof(event), B_RELATIVE_TIMEOUT, 0) == B_OK) {
				result = sd_bus_emit_signal(fBus, NOTIFICATION_PATH,
					NOTIFICATION_INTERFACE, "NotificationClosed", "uu",
					event.id, event.reason);
				if (result < 0) {
					fprintf(stderr, "notification_server: emitting "
						"NotificationClosed failed: %s\n",
						strerror(-result));
				}
			}
		}

		result = sd_bus_process(fBus, NULL);
		if (result < 0) {
			fprintf(stderr, "notification_server: processing D-Bus message "
				"failed: %s\n", strerror(-result));
			break;
		}
		if (result > 0)
			continue;

		result = sd_bus_wait(fBus, 250 * 1000);
		if (result < 0 && result != -EINTR) {
			fprintf(stderr, "notification_server: waiting for D-Bus message "
				"failed: %s\n", strerror(-result));
			break;
		}
	}

	return 0;
}


int
NotificationServer::_Notify(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	NotificationServer* server = (NotificationServer*)data;
	const char* appName = NULL;
	const char* appIcon = NULL;
	const char* summary = NULL;
	const char* body = NULL;
	const char* sender = NULL;
	uint32 replacesID;
	int32 expireTimeout;
	int result = sd_bus_message_read(message, "susss", &appName, &replacesID,
		&appIcon, &summary, &body);
	if (result < 0)
		return sd_bus_reply_method_errorf(message,
			"org.freedesktop.DBus.Error.InvalidArgs",
			"invalid notification arguments");

	result = sd_bus_message_skip(message, "asa{sv}");
	if (result < 0 || sd_bus_message_read(message, "i", &expireTimeout) < 0) {
		return sd_bus_reply_method_errorf(message,
			"org.freedesktop.DBus.Error.InvalidArgs",
			"invalid notification actions, hints, or expiration");
	}

	sender = sd_bus_message_get_sender(message);
	if (appName == NULL || *appName == '\0')
		appName = sender != NULL ? sender : "Application";

	uint32 notificationID = 0;
	bool replacing = false;
	BString messageID;
	server->fNotificationLock.Lock();
	if (replacesID != 0
		&& server->fNotificationMessages.find(replacesID)
			!= server->fNotificationMessages.end()) {
		notificationID = replacesID;
		replacing = true;
	} else {
		for (uint64 tries = 0; tries < UINT32_MAX; tries++) {
			notificationID = server->fNextNotificationID++;
			if (notificationID == 0)
				continue;
			if (server->fNotificationMessages.find(notificationID)
				== server->fNotificationMessages.end()) {
				break;
			}
			notificationID = 0;
		}
	}
	if (notificationID != 0) {
		messageID.SetToFormat("xdg:%" B_PRIu32, notificationID);
		server->fNotificationMessages[notificationID] = messageID;
	}
	server->fNotificationLock.Unlock();

	if (notificationID == 0) {
		return sd_bus_reply_method_errorf(message,
			"org.freedesktop.Notifications.Error.Failed",
			"no notification identifiers available");
	}

	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetGroup(appName);
	notification.SetTitle(summary != NULL ? summary : "");
	notification.SetContent(body != NULL ? body : "");
	notification.SetMessageID(messageID);

	BMessage request(kNotificationMessage);
	status_t status = notification.Archive(&request);
	if (status == B_OK)
		status = request.ReplaceString("_appname", appName);
	if (status == B_OK && sender != NULL)
		status = request.ReplaceString("_signature", sender);
	if (status == B_OK && expireTimeout > 0) {
		status = request.AddInt64("timeout",
			(bigtime_t)expireTimeout * 1000);
	} else if (status == B_OK && expireTimeout == 0) {
		status = request.AddInt64("timeout", -1);
	}

	BMessage reply;
	if (status == B_OK) {
		status = BMessenger(server).SendMessage(&request, &reply,
			5000000LL, 5000000LL);
	}
	int32 notificationError = B_ERROR;
	if (status == B_OK)
		reply.FindInt32("error", &notificationError);
	if (status == B_OK && notificationError != B_OK)
		status = notificationError;

	if (status != B_OK) {
		if (!replacing) {
			server->fNotificationLock.Lock();
			server->fNotificationMessages.erase(notificationID);
			server->fNotificationLock.Unlock();
		}
		return sd_bus_reply_method_errorf(message,
			"org.freedesktop.Notifications.Error.Failed",
			"notification could not be displayed");
	}

	return sd_bus_reply_method_return(message, "u", notificationID);
}


int
NotificationServer::_CloseNotification(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	NotificationServer* server = (NotificationServer*)data;
	uint32 notificationID;
	int result = sd_bus_message_read(message, "u", &notificationID);
	if (result < 0)
		return sd_bus_reply_method_errorf(message,
			"org.freedesktop.DBus.Error.InvalidArgs",
			"invalid notification identifier");

	BString messageID;
	server->fNotificationLock.Lock();
	std::map<uint32, BString>::const_iterator it
		= server->fNotificationMessages.find(notificationID);
	if (it != server->fNotificationMessages.end())
		messageID = it->second;
	server->fNotificationLock.Unlock();

	if (messageID.Length() > 0) {
		BMessage request(kNotificationClose);
		request.AddString("message_id", messageID);
		BMessage reply;
		status_t status = BMessenger(server).SendMessage(&request, &reply,
			5000000LL, 5000000LL);
		if (status != B_OK) {
			return sd_bus_reply_method_errorf(message,
				"org.freedesktop.Notifications.Error.Failed",
				"could not close notification");
		}
	}

	return sd_bus_reply_method_return(message, "");
}


int
NotificationServer::_GetCapabilities(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	sd_bus_message* reply = NULL;
	int result = sd_bus_message_new_method_return(message, &reply);
	if (result < 0)
		return result;

	result = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "s");
	if (result >= 0)
		result = sd_bus_message_append(reply, "s", "body");
	if (result >= 0)
		result = sd_bus_message_close_container(reply);
	if (result >= 0)
		result = sd_bus_send(((NotificationServer*)data)->fBus, reply, NULL);

	sd_bus_message_unref(reply);
	return result < 0 ? result : 1;
}


int
NotificationServer::_GetServerInformation(sd_bus_message* message,
	void* data, sd_bus_error* error)
{
	return sd_bus_reply_method_return(message, "ssss",
		"Koan Notification Server", "Koan contributors", "1.0", "1.2");
}


status_t
NotificationServer::GetSupportedSuites(BMessage* msg)
{
	msg->AddString("suites", "suite/x-vnd.vos-notification_server");

	BPropertyInfo info(main_prop_list);
	msg->AddFlat("messages", &info);

	return BApplication::GetSupportedSuites(msg);
}


BHandler*
NotificationServer::ResolveSpecifier(BMessage* msg, int32 index,
	BMessage* spec, int32 from, const char* prop)
{
	BPropertyInfo info(main_prop_list);

	if (strcmp(prop, "message") == 0) {
		BMessenger messenger(fWindow);
		messenger.SendMessage(msg, fWindow);
		return NULL;
	}

	return BApplication::ResolveSpecifier(msg, index, spec, from, prop);
}


// #pragma mark -


int
main(int argc, char* argv[])
{
	int32 i = 0;

	// Add system sounds
	while (kSoundNames[i] != NULL)
		add_system_beep_event(kSoundNames[i++], 0);

	// Start!
	status_t error;
	NotificationServer server(error);
	if (error == B_OK)
		server.Run();

	return error == B_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
