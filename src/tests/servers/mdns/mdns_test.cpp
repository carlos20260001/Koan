/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	mdns_test <name>.local [seconds]: registers the name with mdns_server,
	resolves it with getaddrinfo() (multicast DNS, answered by mdns_server),
	keeps it for that many seconds for other machines to ask, then
	unregisters it; after that, it must no longer resolve.
*/


#include <Application.h>
#include <Messenger.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <mdns_server.h>


static bool
resolve(const char* name)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	struct addrinfo* result = NULL;
	bigtime_t start = system_time();
	int error = getaddrinfo(name, NULL, &hints, &result);
	printf("resolving %s (%.0f ms):", name, (system_time() - start) / 1000.0);
	if (error != 0) {
		printf(" %s\n", gai_strerror(error));
		return false;
	}
	for (struct addrinfo* ai = result; ai != NULL; ai = ai->ai_next) {
		char text[32];
		inet_ntop(AF_INET, &((sockaddr_in*)ai->ai_addr)->sin_addr, text,
			sizeof(text));
		printf(" %s", text);
	}
	printf("\n");
	freeaddrinfo(result);
	return true;
}


static status_t
send(uint32 what, const char* name)
{
	BMessenger server(MDNS_SERVER_SIGNATURE);
	if (!server.IsValid()) {
		printf("mdns_server is not running\n");
		return B_NAME_NOT_FOUND;
	}
	BMessage message(what);
	message.AddString("name", name);
	BMessage reply;
	status_t status = server.SendMessage(&message, &reply, 5000000, 5000000);
	if (status == B_OK)
		status = reply.GetInt32("status", B_ERROR);
	return status;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("usage: %s <name>.local [seconds]\n", argv[0]);
		return 2;
	}
	BApplication app("application/x-vnd.vos-mdns-test");
	const char* name = argv[1];
	int seconds = argc > 2 ? atoi(argv[2]) : 0;
	int failures = 0;

	status_t status = send(MDNS_REGISTER_NAME, name);
	printf("register: %s\n", strerror(status));
	if (status != B_OK)
		return 1;
	if (!resolve(name))
		failures++;

	if (seconds > 0) {
		printf("answering for %d s\n", seconds);
		fflush(stdout);
		snooze(seconds * 1000000LL);
	}

	status = send(MDNS_UNREGISTER_NAME, name);
	printf("unregister: %s\n", strerror(status));
	if (status != B_OK)
		failures++;
	if (resolve(name))
		failures++;

	printf("%s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
