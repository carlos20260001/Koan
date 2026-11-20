/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	A multicast DNS responder (RFC 6762) for names programs register, such
	as the random .local names WebRTC uses in place of a machine's addresses.
	It answers A questions for them on every IPv4 interface. Resolving .local
	names is libnetwork's getaddrinfo(), which asks the network itself.
*/


#include "MDNSServer.h"

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <resolv.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include <Autolock.h>

#include <mdns_server.h>


static const uint16 kPort = 5353;
static const in_addr_t kGroup = 0xe00000fb;
	// 224.0.0.251, in host order
static const uint32 kTTL = 120;
static const uint32 kLegacyTTL = 10;
static const int32 kAnnouncements = 2;


static bool
valid_name(const char* name)
{
	size_t length = strlen(name);
	if (length < 7 || length > 250 || strcasecmp(name + length - 6, ".local") != 0)
		return false;
	size_t label = 0;
	for (size_t i = 0; i < length; i++) {
		if (name[i] == '.') {
			if (label == 0 || label > 63)
				return false;
			label = 0;
		} else
			label++;
	}
	return label > 0 && label <= 63;
}


static size_t
encode_name(const char* name, uint8* out)
{
	size_t at = 0;
	const char* start = name;
	while (*start != '\0') {
		const char* dot = strchr(start, '.');
		size_t length = dot != NULL ? dot - start : strlen(start);
		out[at++] = length;
		memcpy(out + at, start, length);
		at += length;
		if (dot == NULL)
			break;
		start = dot + 1;
	}
	out[at++] = 0;
	return at;
}


static size_t
put_answer(uint8* out, const char* name, uint16 rrclass, uint32 ttl,
	in_addr_t address)
{
	size_t at = encode_name(name, out);
	out[at++] = 0;
	out[at++] = ns_t_a;
	out[at++] = rrclass >> 8;
	out[at++] = rrclass & 0xff;
	out[at++] = ttl >> 24;
	out[at++] = (ttl >> 16) & 0xff;
	out[at++] = (ttl >> 8) & 0xff;
	out[at++] = ttl & 0xff;
	out[at++] = 0;
	out[at++] = 4;
	memcpy(out + at, &address, 4);
		// network order
	return at + 4;
}


MDNSServer::MDNSServer(status_t* _error)
	:
	BServer(MDNS_SERVER_SIGNATURE, false, _error),
		// a BServer reads the port launch_daemon made for it, where the
		// first messages wait while it starts
	fLock("mdns records"),
	fRecords(8),
	fSocket(-1),
	fListener(-1),
	fQuitting(false),
	fJoined(4),
	fNextJoin(0)
{
}


MDNSServer::~MDNSServer()
{
	fQuitting = true;
	if (fListener >= 0) {
		status_t ignored;
		wait_for_thread(fListener, &ignored);
	}
	if (fSocket >= 0)
		close(fSocket);
}


void
MDNSServer::ReadyToRun()
{
	fSocket = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fSocket < 0) {
		syslog(LOG_ERR, "mdns_server: no socket: %s", strerror(errno));
		Quit();
		return;
	}

	int one = 1;
	setsockopt(fSocket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	setsockopt(fSocket, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
	unsigned char ttl = 255, loop = 1;
	setsockopt(fSocket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	setsockopt(fSocket, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

	sockaddr_in address;
	memset(&address, 0, sizeof(address));
#ifndef __VOS__
	address.sin_len = sizeof(address);
#endif
	address.sin_family = AF_INET;
	address.sin_port = htons(kPort);
	address.sin_addr.s_addr = INADDR_ANY;
	if (bind(fSocket, (sockaddr*)&address, sizeof(address)) != 0) {
		syslog(LOG_ERR, "mdns_server: cannot use port %d: %s", kPort,
			strerror(errno));
		close(fSocket);
		fSocket = -1;
		Quit();
		return;
	}

	_JoinGroups();

	fListener = spawn_thread(_ListenerEntry, "mdns listener",
		B_NORMAL_PRIORITY, this);
	if (fListener >= 0)
		resume_thread(fListener);
}


bool
MDNSServer::QuitRequested()
{
	fQuitting = true;
	BAutolock locker(fLock);
	for (int32 i = 0; MDNSRecord* record = fRecords.ItemAt(i); i++)
		_Announce(record, 0);
	return true;
}


void
MDNSServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MDNS_REGISTER_NAME:
		{
			const char* name;
			const char* addressText;
			status_t status = message->FindString("name", &name);
			if (status == B_OK && !valid_name(name))
				status = B_BAD_VALUE;
			in_addr_t address = 0;
			if (status == B_OK
				&& message->FindString("address", &addressText) == B_OK
				&& inet_pton(AF_INET, addressText, &address) != 1) {
				status = B_BAD_VALUE;
			}

			if (status == B_OK) {
				BAutolock locker(fLock);
				BString lower(name);
				lower.ToLower();
				MDNSRecord* record = _Find(lower);
				if (record != NULL
					&& record->owner.Team() != message->ReturnAddress().Team()
					&& record->owner.IsValid()) {
					status = B_NAME_IN_USE;
				} else {
					if (record == NULL) {
						record = new MDNSRecord;
						record->name = lower;
						fRecords.AddItem(record);
					}
					record->address = address;
					record->owner = message->ReturnAddress();
					record->announcements = kAnnouncements;
					record->nextAnnouncement = 0;
				}
			}

			BMessage reply(B_REPLY);
			reply.AddInt32("status", status);
			message->SendReply(&reply);
			break;
		}

		case MDNS_UNREGISTER_NAME:
		{
			const char* name;
			status_t status = message->FindString("name", &name);
			if (status == B_OK) {
				BAutolock locker(fLock);
				BString lower(name);
				lower.ToLower();
				MDNSRecord* record = _Find(lower);
				if (record == NULL)
					status = B_NAME_NOT_FOUND;
				else {
					_Announce(record, 0);
					fRecords.RemoveItem(record);
				}
			}
			BMessage reply(B_REPLY);
			reply.AddInt32("status", status);
			message->SendReply(&reply);
			break;
		}

		default:
			BServer::MessageReceived(message);
	}
}


status_t
MDNSServer::_ListenerEntry(void* self)
{
	((MDNSServer*)self)->_Listen();
	return B_OK;
}


void
MDNSServer::_Listen()
{
	uint8 packet[9000];
	while (!fQuitting) {
		struct pollfd pfd = { fSocket, POLLIN, 0 };
		if (poll(&pfd, 1, 500) > 0) {
			sockaddr_in from;
			socklen_t fromLength = sizeof(from);
			ssize_t size = recvfrom(fSocket, packet, sizeof(packet), 0,
				(sockaddr*)&from, &fromLength);
			if (size >= 12) {
				BAutolock locker(fLock);
				_HandlePacket(packet, size, from);
			}
		}
		BAutolock locker(fLock);
		_Housekeeping();
	}
}


/*!	Joins the group on interfaces that came up since; the network may change
	while this runs. Loopback too: a program here asking for a name this
	answers gets its own query on the loopback interface.
*/
void
MDNSServer::_JoinGroups()
{
	struct ifaddrs* interfaces;
	if (getifaddrs(&interfaces) != 0)
		return;

	for (struct ifaddrs* interface = interfaces; interface != NULL;
			interface = interface->ifa_next) {
		if (interface->ifa_addr == NULL
			|| interface->ifa_addr->sa_family != AF_INET
			|| (interface->ifa_flags & IFF_UP) == 0)
			continue;
		in_addr local = ((sockaddr_in*)interface->ifa_addr)->sin_addr;
		bool joined = false;
		for (int32 i = 0; in_addr* address = fJoined.ItemAt(i); i++) {
			if (address->s_addr == local.s_addr)
				joined = true;
		}
		if (joined)
			continue;

		ip_mreq request;
		request.imr_multiaddr.s_addr = htonl(kGroup);
		request.imr_interface = local;
		if (setsockopt(fSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request,
				sizeof(request)) == 0 || errno == EADDRINUSE) {
			fJoined.AddItem(new in_addr(local));
		}
	}
	freeifaddrs(interfaces);
	fNextJoin = system_time() + 10000000;
}


void
MDNSServer::_Housekeeping()
{
	bigtime_t now = system_time();
	if (now >= fNextJoin)
		_JoinGroups();

	for (int32 i = fRecords.CountItems() - 1; i >= 0; i--) {
		MDNSRecord* record = fRecords.ItemAt(i);
		if (!record->owner.IsValid()) {
			// the program that registered it is gone
			_Announce(record, 0);
			fRecords.RemoveItemAt(i);
			continue;
		}
		if (record->announcements > 0 && now >= record->nextAnnouncement) {
			_Announce(record, kTTL);
			record->announcements--;
			record->nextAnnouncement = now + 1000000;
		}
	}
}


MDNSRecord*
MDNSServer::_Find(const char* name)
{
	for (int32 i = 0; MDNSRecord* record = fRecords.ItemAt(i); i++) {
		if (record->name == name)
			return record;
	}
	return NULL;
}


/*!	The address to answer with: the registered one, or the one of the
	interface on the querier's network (any other interface's if there is
	none, as for a querier on this machine).
*/
in_addr_t
MDNSServer::_AddressFor(const MDNSRecord* record, in_addr_t querier)
{
	if (record->address != 0)
		return record->address;

	in_addr_t fallback = 0;
	struct ifaddrs* interfaces;
	if (getifaddrs(&interfaces) != 0)
		return 0;
	for (struct ifaddrs* interface = interfaces; interface != NULL;
			interface = interface->ifa_next) {
		if (interface->ifa_addr == NULL || interface->ifa_netmask == NULL
			|| interface->ifa_addr->sa_family != AF_INET
			|| (interface->ifa_flags & (IFF_UP | IFF_LOOPBACK)) != IFF_UP)
			continue;
		in_addr_t local = ((sockaddr_in*)interface->ifa_addr)->sin_addr.s_addr;
		in_addr_t mask = ((sockaddr_in*)interface->ifa_netmask)->sin_addr.s_addr;
		if (fallback == 0)
			fallback = local;
		if ((local & mask) == (querier & mask)) {
			freeifaddrs(interfaces);
			return local;
		}
	}
	freeifaddrs(interfaces);
	return fallback;
}


void
MDNSServer::_HandlePacket(const uint8* packet, size_t size,
	const sockaddr_in& from)
{
	uint16 flags = (packet[2] << 8) | packet[3];
	if ((flags & 0x8000) != 0 || (flags & 0x7800) != 0)
		return;
		// a response, or not a standard query

	uint16 questions = (packet[4] << 8) | packet[5];
	const uint8* end = packet + size;
	const uint8* at = packet + 12;
	bool legacy = ntohs(from.sin_port) != kPort;

	for (int i = 0; i < questions; i++) {
		char name[MAXDNAME];
		const uint8* question = at;
		int length = dn_expand(packet, end, at, name, sizeof(name));
		if (length < 0 || at + length + 4 > end)
			return;
		at += length;
		uint16 type = (at[0] << 8) | at[1];
		uint16 rrclass = (at[2] << 8) | at[3];
		at += 4;
		if ((type != ns_t_a && type != ns_t_any)
			|| (rrclass & 0x7fff) != ns_c_in)
			continue;

		BString lower(name);
		lower.ToLower();
		if (lower.EndsWith("."))
			lower.Truncate(lower.Length() - 1);
		MDNSRecord* record = _Find(lower);
		if (record == NULL)
			continue;

		if (legacy) {
			// a one-shot query (RFC 6762 5.1): the answer goes to the port
			// it came from, with its ID and question; the question is
			// rewritten uncompressed
			uint8 encoded[MAXDNAME + 8];
			size_t encodedSize = encode_name(record->name, encoded);
			memcpy(encoded + encodedSize, at - 4, 4);
			encodedSize += 4;
			_SendAnswers(record, &from, kLegacyTTL, encoded, encodedSize,
				(packet[0] << 8) | packet[1]);
		} else if ((rrclass & 0x8000) != 0) {
			// the querier asked for a unicast answer
			_SendAnswers(record, &from, kTTL, NULL, 0, 0);
		} else
			_SendAnswers(record, NULL, kTTL, NULL, 0, 0);
		(void)question;
	}
}


void
MDNSServer::_SendAnswers(MDNSRecord* record, const sockaddr_in* to,
	uint32 ttl, const uint8* question, size_t questionSize, uint16 queryID)
{
	uint8 packet[600];
	memset(packet, 0, 12);
	packet[0] = queryID >> 8;
	packet[1] = queryID & 0xff;
	packet[2] = 0x84;
		// a response, authoritative
	size_t at = 12;
	if (question != NULL) {
		packet[5] = 1;
		memcpy(packet + at, question, questionSize);
		at += questionSize;
	}
	packet[7] = 1;

	in_addr_t querier = to != NULL ? to->sin_addr.s_addr : 0;
	in_addr_t address = _AddressFor(record, querier);
	if (address == 0)
		return;
	// the cache flush bit marks the record as this responder's only; not in
	// answers to one-shot queries
	at += put_answer(packet + at, record->name,
		question != NULL ? ns_c_in : (ns_c_in | 0x8000), ttl, address);

	sockaddr_in destination;
	if (to != NULL)
		destination = *to;
	else {
		memset(&destination, 0, sizeof(destination));
#ifndef __VOS__
		destination.sin_len = sizeof(destination);
#endif
		destination.sin_family = AF_INET;
		destination.sin_port = htons(kPort);
		destination.sin_addr.s_addr = htonl(kGroup);
	}
	sendto(fSocket, packet, at, 0, (sockaddr*)&destination,
		sizeof(destination));
}


/*!	Tells the network about a record (or, with TTL 0, that it is gone), on
	every interface with that interface's address.
*/
void
MDNSServer::_Announce(MDNSRecord* record, uint32 ttl)
{
	for (int32 i = 0; in_addr* local = fJoined.ItemAt(i); i++) {
		if ((ntohl(local->s_addr) >> 24) == 127)
			continue;
		setsockopt(fSocket, IPPROTO_IP, IP_MULTICAST_IF, local, sizeof(*local));

		uint8 packet[600];
		memset(packet, 0, 12);
		packet[2] = 0x84;
		packet[7] = 1;
		size_t at = 12 + put_answer(packet + 12, record->name,
			ns_c_in | 0x8000, ttl,
			record->address != 0 ? record->address : local->s_addr);

		sockaddr_in destination;
		memset(&destination, 0, sizeof(destination));
#ifndef __VOS__
		destination.sin_len = sizeof(destination);
#endif
		destination.sin_family = AF_INET;
		destination.sin_port = htons(kPort);
		destination.sin_addr.s_addr = htonl(kGroup);
		sendto(fSocket, packet, at, 0, (sockaddr*)&destination,
			sizeof(destination));
	}
	in_addr any;
	any.s_addr = INADDR_ANY;
	setsockopt(fSocket, IPPROTO_IP, IP_MULTICAST_IF, &any, sizeof(any));
}


int
main()
{
	status_t status;
	MDNSServer server(&status);
	if (status != B_OK)
		return 1;
	server.Run();
	return 0;
}
