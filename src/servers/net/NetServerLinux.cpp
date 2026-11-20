/*
 * Copyright 2006-2019, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	net_server on Linux.

	On Haiku the net_server configures the interfaces of the kernel's
	network stack, runs DHCP and IPv6 autoconfiguration, joins wireless
	networks through the wpa_supplicant and starts the network services
	(NetServer.cpp, AutoconfigLooper.cpp, DHCPClient.cpp). On Linux
	NetworkManager does the interfaces, DHCP, autoconfiguration and
	wireless; the network kit (libnetapi) talks to it directly (NMBackend).
	What air/OS's preferences and kit still ask the net_server for stays
	here, with the same messages (<NetServer.h>):
	- the saved wireless networks (~/config/settings/network/wireless_networks,
	  BNetworkSettings): count, get, add, remove;
	- joining and leaving a network: handed to NetworkManager through
	  BNetworkDevice, with the saved password and authentication when the
	  request has none;
	- auto-joining (wifiautojoin): a wireless device that is not associated
	  joins the first saved network in range, which also leaves a
	  NetworkManager connection profile that autoconnects later;
	- the network services (Services.cpp: inetd-style listeners for the
	  services in the network settings) and kMsgIsServiceRunning.
*/


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <NetworkDevice.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <NetworkSettings.h>
#include <PathMonitor.h>
#include <Server.h>

#include <NetServer.h>

#include "Services.h"


using namespace BNetworkKit;


class NetServer : public BServer {
public:
								NetServer(status_t& status);

	virtual	void				ReadyToRun();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_StartServices();
			bool				_FindSavedNetwork(const char* name,
									BMessage& network);
			status_t			_JoinNetwork(const BMessage& message,
									const char* name = NULL);
			status_t			_LeaveNetwork(const BMessage& message);
			status_t			_AutoJoinNetwork(const char* device);
			void				_AutoJoinNetworks();

private:
			BNetworkSettings	fSettings;
			BMessenger			fServices;
};


NetServer::NetServer(status_t& error)
	:
	BServer(kNetServerSignature, false, &error)
{
}


void
NetServer::ReadyToRun()
{
	fSettings.StartMonitoring(this);
	_StartServices();
	_AutoJoinNetworks();
}


void
NetServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_PATH_MONITOR:
			fSettings.Update(message);
			break;

		case BNetworkSettings::kMsgInterfaceSettingsUpdated:
			// NetworkManager owns the interface configuration
			break;

		case BNetworkSettings::kMsgServiceSettingsUpdated:
		{
			BMessage update = fSettings.Services();
			update.what = kMsgUpdateServices;
			fServices.SendMessage(&update);
			break;
		}

		case kMsgConfigureInterface:
		case kMsgConfigureResolver:
		case kMsgSetServiceOrder:
		{
			// TODO(linux backend): static addresses, the resolver and the
			// service order are NetworkManager connection settings
			// (ipv4.method/addresses, ipv4.dns, autoconnect-priority); the
			// network kit does not map them yet.
			BMessage reply(B_REPLY);
			reply.AddInt32("status", B_NOT_SUPPORTED);
			message->SendReply(&reply);
			break;
		}

		case kMsgJoinNetwork:
		{
			BMessage reply(B_REPLY);
			reply.AddInt32("status", _JoinNetwork(*message));
			message->SendReply(&reply);
			break;
		}

		case kMsgLeaveNetwork:
		{
			BMessage reply(B_REPLY);
			reply.AddInt32("status", _LeaveNetwork(*message));
			message->SendReply(&reply);
			break;
		}

		case kMsgAutoJoinNetwork:
		{
			const char* device;
			if (message->FindString("device", &device) == B_OK)
				_AutoJoinNetwork(device);
			break;
		}

		case kMsgCountPersistentNetworks:
		{
			BMessage reply(B_REPLY);
			reply.AddInt32("count", fSettings.CountNetworks());
			message->SendReply(&reply);
			break;
		}

		case kMsgGetPersistentNetwork:
		{
			uint32 index = 0;
			status_t result = message->FindInt32("index", (int32*)&index);

			BMessage reply(B_REPLY);
			if (result == B_OK) {
				BMessage network;
				result = fSettings.GetNextNetwork(index, network);
				if (result == B_OK)
					result = reply.AddMessage("network", &network);
			}

			reply.AddInt32("status", result);
			message->SendReply(&reply);
			break;
		}

		case kMsgAddPersistentNetwork:
		{
			BMessage network = *message;
			status_t result = fSettings.AddNetwork(network);

			BMessage reply(B_REPLY);
			reply.AddInt32("status", result);
			message->SendReply(&reply);
			break;
		}

		case kMsgRemovePersistentNetwork:
		{
			const char* networkName = NULL;
			status_t result = message->FindString("name", &networkName);
			if (result == B_OK) {
				result = fSettings.RemoveNetwork(networkName);
				// and NetworkManager's profile, so that it does not join it
				// on its own any longer
				BNetworkRoster::Default().RemovePersistentNetwork(
					networkName);
			}

			BMessage reply(B_REPLY);
			reply.AddInt32("status", result);
			message->SendReply(&reply);
			break;
		}

		case kMsgIsServiceRunning:
		{
			BHandler* handler = fServices.Target(NULL);
			if (handler != NULL)
				handler->MessageReceived(message);
			break;
		}

		default:
			BApplication::MessageReceived(message);
			return;
	}
}


void
NetServer::_StartServices()
{
	BHandler* services = new (std::nothrow) Services(fSettings.Services());
	if (services != NULL) {
		AddHandler(services);
		fServices = BMessenger(services);
	}
}


bool
NetServer::_FindSavedNetwork(const char* name, BMessage& network)
{
	uint32 cookie = 0;
	while (fSettings.GetNextNetwork(cookie, network) == B_OK) {
		const char* networkName;
		if (network.FindString("name", &networkName) == B_OK
			&& strcmp(name, networkName) == 0)
			return true;
	}
	return false;
}


status_t
NetServer::_JoinNetwork(const BMessage& message, const char* name)
{
	const char* deviceName;
	if (message.FindString("device", &deviceName) != B_OK)
		return B_BAD_VALUE;
	if (name == NULL && message.FindString("name", &name) != B_OK)
		return B_BAD_VALUE;
		// TODO(linux backend): joining by the access point's address

	// the saved password when the request has none
	BMessage saved;
	const char* password = NULL;
	if (message.FindString("password", &password) != B_OK
		&& _FindSavedNetwork(name, saved))
		saved.FindString("password", &password);

	BNetworkDevice device(deviceName);
	status_t status = device.JoinNetwork(name, password);
	printf("net_server: joining \"%s\" on %s: %s\n", name, deviceName,
		strerror(status));
	return status;
}


status_t
NetServer::_LeaveNetwork(const BMessage& message)
{
	const char* deviceName;
	if (message.FindString("device", &deviceName) != B_OK)
		return B_BAD_VALUE;

	BNetworkDevice device(deviceName);
	wireless_network network;
	uint32 cookie = 0;
	const char* name;
	if (message.FindString("name", &name) != B_OK) {
		if (device.GetNextAssociatedNetwork(cookie, network) != B_OK)
			return B_ERROR;
		name = network.name;
	}
	return device.LeaveNetwork(name);
}


/*!	A wireless device that is not associated joins the first saved network
	in range, as air/OS's AutoconfigLooper asks for when a link has no
	network. */
status_t
NetServer::_AutoJoinNetwork(const char* deviceName)
{
	BNetworkDevice device(deviceName);
	if (!device.IsWireless())
		return B_BAD_VALUE;

	wireless_network network;
	uint32 cookie = 0;
	if (device.GetNextAssociatedNetwork(cookie, network) == B_OK)
		return B_OK;

	cookie = 0;
	BMessage saved;
	while (fSettings.GetNextNetwork(cookie, saved) == B_OK) {
		const char* name;
		if (saved.FindString("name", &name) != B_OK
			|| device.GetNetwork(name, network) != B_OK)
			continue;

		BMessage join;
		join.AddString("device", deviceName);
		status_t status = _JoinNetwork(join, network.name);
		printf("net_server: auto join network \"%s\": %s\n", network.name,
			strerror(status));
		if (status == B_OK)
			return B_OK;
	}

	return B_NO_INIT;
}


void
NetServer::_AutoJoinNetworks()
{
	if (fSettings.CountNetworks() == 0)
		return;

	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	while (roster.GetNextInterface(&cookie, interface) == B_OK)
		_AutoJoinNetwork(interface.Name());
}


//	#pragma mark -


int
main(int argc, char** argv)
{
	status_t status;
	NetServer server(status);
	if (status != B_OK) {
		fprintf(stderr, "net_server: Failed to create application: %s\n",
			strerror(status));
		return 1;
	}

	server.Run();
	return 0;
}
