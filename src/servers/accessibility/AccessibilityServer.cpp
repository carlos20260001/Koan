/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


/*!	The accessibility_server (haiku#54) connects applications to assistive
	technologies such as the screen reader.

	It is an on-demand launch_daemon service: it starts when an assistive
	technology subscribes. While it has at least one client it asks every
	running and newly launched application to enable its accessibility
	bridge, and passes the applications' events on to the clients. When the
	last client leaves it disables the bridges again, so that applications
	send nothing, and quits after a while. See docs/airos/ACCESSIBILITY.md.
*/


#include <map>
#include <set>

#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <Accessible.h>
#include <AccessibilityProtocol.h>
#include <List.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Roster.h>
#include <Server.h>
#include <String.h>


static const uint32 kMsgIdleCheck = 'aIdl';
static const bigtime_t kIdleQuitDelay = 30000000;
static const bigtime_t kSendTimeout = 100000;


struct AppInfo {
	BString		signature;
	BMessenger	bridge;
};

struct ClientInfo {
	BMessenger	messenger;
	team_id		team;
};


class AccessibilityServer : public BServer {
public:
								AccessibilityServer(status_t& error);

	virtual	void				ReadyToRun();
	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Subscribe(BMessage* message);
			void				_Unsubscribe(const BMessenger& client);
			void				_RemoveClientTeam(team_id team);
			void				_AddAppList(BMessage& reply);
			void				_Register(BMessage* message);
			void				_ForwardEvent(BMessage* message);
			void				_SendToClients(BMessage& message);
			void				_AppLaunched(BMessage* message);
			void				_AppQuit(BMessage* message);

			bool				_IsEligible(team_id team) const;
			void				_Enable(team_id team);
			void				_EnableAll();
			void				_DisableAll();
			bool				_IsClientTeam(team_id team) const;

private:
			std::map<team_id, AppInfo> fApps;
			std::set<team_id>	fEnabled;
			BList				fClients;
			BMessageRunner*		fIdleRunner;
};


AccessibilityServer::AccessibilityServer(status_t& error)
	:
	BServer(B_ACCESSIBILITY_SERVER_SIGNATURE, false, &error),
	fIdleRunner(NULL)
{
}


void
AccessibilityServer::ReadyToRun()
{
	be_roster->StartWatching(BMessenger(this),
		B_REQUEST_LAUNCHED | B_REQUEST_QUIT);

	// quit if nobody subscribes (for example after a stray launch)
	BMessage idle(kMsgIdleCheck);
	fIdleRunner = new BMessageRunner(BMessenger(this), &idle, kIdleQuitDelay,
		1);
}


bool
AccessibilityServer::QuitRequested()
{
	be_roster->StopWatching(BMessenger(this));
	_DisableAll();

	for (int32 i = 0; i < fClients.CountItems(); i++)
		delete (ClientInfo*)fClients.ItemAt(i);
	fClients.MakeEmpty();
	delete fIdleRunner;
	fIdleRunner = NULL;
	return true;
}


void
AccessibilityServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_ACCESSIBILITY_SUBSCRIBE:
			_Subscribe(message);
			break;

		case B_ACCESSIBILITY_UNSUBSCRIBE:
		{
			BMessenger client;
			if (message->FindMessenger("client", &client) == B_OK)
				_Unsubscribe(client);
			break;
		}

		case B_ACCESSIBILITY_GET_APPS:
		{
			BMessage reply(B_ACCESSIBILITY_REPLY);
			_AddAppList(reply);
			reply.AddInt32("status", B_OK);
			message->SendReply(&reply);
			break;
		}

		case B_ACCESSIBILITY_REGISTER:
			_Register(message);
			break;

		case B_ACCESSIBILITY_EVENT:
			_ForwardEvent(message);
			break;

		case B_SOME_APP_LAUNCHED:
			_AppLaunched(message);
			break;

		case B_SOME_APP_QUIT:
			_AppQuit(message);
			break;

		case kMsgIdleCheck:
			delete fIdleRunner;
			fIdleRunner = NULL;
			if (fClients.IsEmpty()) {
				syslog(LOG_INFO, "accessibility_server: no clients, quitting");
				PostMessage(B_QUIT_REQUESTED);
			}
			break;

		default:
			BServer::MessageReceived(message);
			break;
	}
}


void
AccessibilityServer::_Subscribe(BMessage* message)
{
	BMessenger client;
	if (message->FindMessenger("client", &client) != B_OK)
		client = message->ReturnAddress();

	bool known = false;
	for (int32 i = 0; i < fClients.CountItems(); i++) {
		if (((ClientInfo*)fClients.ItemAt(i))->messenger == client)
			known = true;
	}

	if (!known) {
		ClientInfo* info = new ClientInfo;
		info->messenger = client;
		info->team = client.Team();
		fClients.AddItem(info);
		syslog(LOG_INFO, "accessibility_server: client team %" B_PRId32
			" subscribed (%" B_PRId32 " clients)", info->team,
			fClients.CountItems());

		delete fIdleRunner;
		fIdleRunner = NULL;

		// a client's own windows are not passed on to the clients
		if (fEnabled.find(info->team) != fEnabled.end()) {
			BMessenger app(NULL, info->team);
			BMessage disable(B_ACCESSIBILITY_DISABLE);
			app.SendMessage(&disable, (BHandler*)NULL, kSendTimeout);
			fEnabled.erase(info->team);
			fApps.erase(info->team);
		}

		if (fClients.CountItems() == 1)
			_EnableAll();
	}

	BMessage reply(B_ACCESSIBILITY_REPLY);
	_AddAppList(reply);
	reply.AddInt32("status", B_OK);
	message->SendReply(&reply);
}


void
AccessibilityServer::_Unsubscribe(const BMessenger& client)
{
	for (int32 i = 0; i < fClients.CountItems(); i++) {
		ClientInfo* info = (ClientInfo*)fClients.ItemAt(i);
		if (info->messenger == client) {
			fClients.RemoveItem(i);
			delete info;
			break;
		}
	}

	if (fClients.IsEmpty()) {
		_DisableAll();
		if (fIdleRunner == NULL) {
			BMessage idle(kMsgIdleCheck);
			fIdleRunner = new BMessageRunner(BMessenger(this), &idle,
				kIdleQuitDelay, 1);
		}
	}
}


void
AccessibilityServer::_RemoveClientTeam(team_id team)
{
	for (int32 i = fClients.CountItems() - 1; i >= 0; i--) {
		ClientInfo* info = (ClientInfo*)fClients.ItemAt(i);
		if (info->team == team) {
			BMessenger messenger = info->messenger;
			_Unsubscribe(messenger);
		}
	}
}


void
AccessibilityServer::_AddAppList(BMessage& reply)
{
	std::map<team_id, AppInfo>::iterator it = fApps.begin();
	for (; it != fApps.end(); it++) {
		BMessage app;
		app.AddInt32("team", it->first);
		app.AddString("signature", it->second.signature);
		app.AddMessenger("bridge", it->second.bridge);
		reply.AddMessage("app", &app);
	}
}


void
AccessibilityServer::_Register(BMessage* message)
{
	team_id team = message->GetInt32("team", -1);
	if (team < 0)
		team = message->ReturnAddress().Team();
	if (fClients.IsEmpty() || _IsClientTeam(team)) {
		// a late registration after the last client left
		BMessenger app(NULL, team);
		BMessage disable(B_ACCESSIBILITY_DISABLE);
		app.SendMessage(&disable, (BHandler*)NULL, kSendTimeout);
		return;
	}

	AppInfo& info = fApps[team];
	info.signature = message->GetString("signature", "");
	message->FindMessenger("bridge", &info.bridge);
	fEnabled.insert(team);

	BMessage event(B_ACCESSIBILITY_EVENT);
	event.AddInt32("event", B_ACCESSIBLE_EVENT_APP_REGISTERED);
	event.AddInt32("team", team);
	event.AddString("signature", info.signature);
	event.AddMessenger("bridge", info.bridge);
	_SendToClients(event);
}


void
AccessibilityServer::_ForwardEvent(BMessage* message)
{
	if (fClients.IsEmpty())
		return;

	team_id team = message->ReturnAddress().Team();
	std::map<team_id, AppInfo>::iterator found = fApps.find(team);
	message->RemoveName("team");
	message->AddInt32("team", team);
	if (found != fApps.end()) {
		message->AddString("signature", found->second.signature);
		message->AddMessenger("bridge", found->second.bridge);
	}
	_SendToClients(*message);
}


void
AccessibilityServer::_SendToClients(BMessage& message)
{
	for (int32 i = fClients.CountItems() - 1; i >= 0; i--) {
		ClientInfo* info = (ClientInfo*)fClients.ItemAt(i);
		status_t status = info->messenger.SendMessage(&message,
			(BHandler*)NULL, kSendTimeout);
		if (status == B_BAD_PORT_ID) {
			BMessenger messenger = info->messenger;
			_Unsubscribe(messenger);
		}
	}
}


void
AccessibilityServer::_AppLaunched(BMessage* message)
{
	team_id team = message->GetInt32("be:team", -1);
	if (team < 0 || fClients.IsEmpty() || _IsClientTeam(team))
		return;
	_Enable(team);
}


void
AccessibilityServer::_AppQuit(BMessage* message)
{
	team_id team = message->GetInt32("be:team", -1);
	if (team < 0)
		return;

	_RemoveClientTeam(team);
	fEnabled.erase(team);
	if (fApps.erase(team) > 0) {
		BMessage event(B_ACCESSIBILITY_EVENT);
		event.AddInt32("event", B_ACCESSIBLE_EVENT_APP_QUIT);
		event.AddInt32("team", team);
		_SendToClients(event);
	}
}


bool
AccessibilityServer::_IsEligible(team_id team) const
{
	if (team == Team())
		return false;

	app_info info;
	if (be_roster->GetRunningAppInfo(team, &info) != B_OK)
		return false;
	if ((info.flags & B_ARGV_ONLY) != 0)
		return false;

	// system servers that are not BApplications or have no user interface
	static const char* kSkip[] = {
		"application/x-vnd.vos-app_server",
		"application/x-vnd.vos-registrar",
		"application/x-vnd.vos-debug_server",
		"application/x-vnd.vos-launch_daemon",
		"application/x-vnd.vos-package_daemon",
		"application/x-vnd.vos-net_server",
		"application/x-vnd.vos-syslog_daemon",
		NULL
	};
	for (int32 i = 0; kSkip[i] != NULL; i++) {
		if (strcasecmp(info.signature, kSkip[i]) == 0)
			return false;
	}
	return true;
}


void
AccessibilityServer::_Enable(team_id team)
{
	if (!_IsEligible(team))
		return;

	BMessenger app(NULL, team);
	if (!app.IsValid())
		return;

	BMessage enable(B_ACCESSIBILITY_ENABLE);
	enable.AddMessenger("server", BMessenger(this));
	if (app.SendMessage(&enable, (BHandler*)NULL, kSendTimeout) == B_OK)
		fEnabled.insert(team);
}


void
AccessibilityServer::_EnableAll()
{
	BList teams;
	be_roster->GetAppList(&teams);
	for (int32 i = 0; i < teams.CountItems(); i++) {
		team_id team = (team_id)(addr_t)teams.ItemAt(i);
		if (!_IsClientTeam(team))
			_Enable(team);
	}
	syslog(LOG_INFO, "accessibility_server: enabled %" B_PRIuSIZE
		" applications", fEnabled.size());
}


void
AccessibilityServer::_DisableAll()
{
	std::set<team_id>::iterator it = fEnabled.begin();
	for (; it != fEnabled.end(); it++) {
		BMessenger app(NULL, *it);
		BMessage disable(B_ACCESSIBILITY_DISABLE);
		app.SendMessage(&disable, (BHandler*)NULL, kSendTimeout);
	}
	if (!fEnabled.empty()) {
		syslog(LOG_INFO, "accessibility_server: disabled %" B_PRIuSIZE
			" applications", fEnabled.size());
	}
	fEnabled.clear();
	fApps.clear();
}


bool
AccessibilityServer::_IsClientTeam(team_id team) const
{
	for (int32 i = 0; i < fClients.CountItems(); i++) {
		if (((ClientInfo*)fClients.ItemAt(i))->team == team)
			return true;
	}
	return false;
}


//	#pragma mark -


int
main(int argc, char** argv)
{
	status_t error;
	AccessibilityServer server(error);
	if (error != B_OK) {
		fprintf(stderr, "accessibility_server: %s\n", strerror(error));
		return 1;
	}

	server.Run();
	return 0;
}
