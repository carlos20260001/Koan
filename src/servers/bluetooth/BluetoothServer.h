/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _BLUETOOTH_SERVER_APP_H
#define _BLUETOOTH_SERVER_APP_H

#include <stdlib.h>

#include <Application.h>
#include <ObjectList.h>
#include <OS.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>
#include <bluetooth/HCI/btHCI_transport.h>
#include <bluetooth/HCI/btHCI_command.h>

#include "DeviceManager.h"
#include "HCIDelegate.h"
#include "HIDHost.h"
#include "LocalDeviceImpl.h"
#include "SDPServer.h"
#include "SDPClient.h"

#include <PortListener.h>

#ifdef __VOS__
#	include "BlueZAgent.h"
#endif

#define BT "bluetooth_server: "

// Logs a step of bringing up the radios, with the time since boot.
void LogStartup(const char* format, ...)
	__attribute__((format(printf, 1, 2)));

typedef enum {
	BLACKBOARD_GENERAL = 0,
	BLACKBOARD_DEVICEMANAGER,
	BLACKBOARD_KIT,
	BLACKBOARD_SDP,
	// more blackboards
	BLACKBOARD_END
} BluetoothServerBlackBoardIndex;

#define BLACKBOARD_LD(X) (BLACKBOARD_END+X-HCI_DEVICE_INDEX_OFFSET)

typedef BObjectList<LocalDeviceImpl> LocalDevicesList;
typedef BObjectList<BMessenger> WatchersList;
typedef PortListener<struct hci_event_header, 
	HCI_MAX_EVENT_SIZE, // Event Body can hold max 255 + 2 header
	24					// Some devices have sent chunks of 24 events(inquiry result)
	> BluetoothPortListener;

class BluetoothServer : public BApplication
{
public:

	BluetoothServer();

	virtual bool QuitRequested(void);
	virtual void ArgvReceived(int32 argc, char **argv);
	virtual void ReadyToRun(void);
	virtual void Pulse(void);


	virtual void AppActivated(bool act);
	virtual void MessageReceived(BMessage *message);

	static int32 SDPServerThread(void* data);
	
	/* Messages reply */
	status_t	HandleLocalDevicesCount(BMessage* message, BMessage* reply);
	status_t    HandleAcquireLocalDevice(BMessage* message, BMessage* reply);
	
	status_t    HandleGetProperty(BMessage* message, BMessage* reply);
	status_t    HandleSimpleRequest(BMessage* message, BMessage* reply);

	void		NotifyWatchers(BMessage* notice);
	void		DiscoverServices(ServerRemoteDevice* rd);
	void		NotifyServices(ServerRemoteDevice* rd);
	HIDHost*	GetHIDHost() { return fHIDHost; }
	void		CheckLinkSecurityLater(hci_id hid, const bdaddr_t& address,
					int32 attempt);

    LocalDeviceImpl*    LocateLocalDeviceImpl(hci_id hid);
	
private:

	LocalDeviceImpl*	LocateDelegateFromMessage(BMessage* message);
	static status_t		_DiscoverServicesThread(void* data);
	void				_ServicesDiscovered(BMessage* message);

	void 				ShowWindow(BWindow* pWindow);


	LocalDevicesList   	fLocalDevicesList;
	BMessage			fDevicePaths;
		// the hci_id of each radio opened, by its path
	WatchersList		fWatchersList;


	// Notification system
	BluetoothPortListener*	fEventListener2;
	
	DeviceManager*			fDeviceManager;
	
	BPoint 					fCenter;
	
	SDPServer*              fSDPServer;
	SDPClient*				fSDPClient;
	HIDHost*				fHIDHost;
#ifdef __VOS__
	BlueZAgent				fAgent;
#endif
};

#endif
