/* Ask the mount server about the network shares, and to mount or unmount one.
 *
 * What Tracker's preferences do with a mouse, for where there is none: the
 * check script asks which shares there are and whether they are mounted, and
 * a share can be mounted over ssh.
 *
 * usage: sharectl list
 *        sharectl mount <name>
 *        sharectl unmount <name>
 *
 * list prints a line per share: its name, where it is, "startup" or "manual",
 * and the mount point or "unmounted", followed by the error of the last try
 * if there was one.
 *
 * Build on the machine: g++ -o sharectl sharectl.cpp -lbe
 */
#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>


// headers/private/mount/MountServer.h
static const uint32 kGetNetworkShares = 'gnsh';
static const uint32 kMountNetworkShare = 'mnsh';
static const uint32 kUnmountNetworkShare = 'unsh';
static const char* kMountServerSignature
	= "application/x-vnd.vos-mount_server";

// mounting waits for the server, which is given twenty seconds and one more
// try
static const bigtime_t kTimeout = 90 * 1000000LL;


int
main(int argc, char** argv)
{
	if (argc < 2 || (strcmp(argv[1], "list") != 0 && argc < 3)) {
		fprintf(stderr, "usage: %s list | mount <name> | unmount <name>\n",
			argv[0]);
		return 2;
	}

	BApplication app("application/x-vnd.x399-sharectl");
	BMessenger mountServer(kMountServerSignature);

	BMessage request(kGetNetworkShares);
	BMessage shares;
	status_t status = mountServer.SendMessage(&request, &shares, kTimeout,
		kTimeout);
	if (status != B_OK || shares.what == B_NO_REPLY) {
		fprintf(stderr, "the mount server does not answer: %s\n",
			status != B_OK ? strerror(status) : "it knows no shares");
		return 1;
	}

	const bool list = strcmp(argv[1], "list") == 0;
	int32 id = -1;

	BMessage share;
	for (int32 i = 0; shares.FindMessage("share", i, &share) == B_OK; i++) {
		if (list) {
			BString where;
			where.SetToFormat("//%s/%s", share.GetString("server", ""),
				share.GetString("share", ""));
			if (share.GetString("path", "")[0] != '\0')
				where << "/" << share.GetString("path", "");

			printf("%s\t%s\t%s\t%s", share.GetString("name", ""),
				where.String(),
				share.GetBool("mount at startup", false)
					? "startup" : "manual",
				share.GetBool("mounted", false)
					? share.GetString("mount point", "") : "unmounted");
			if (share.GetInt32("error", B_OK) != B_OK)
				printf("\t%s", strerror(share.GetInt32("error", B_OK)));
			printf("\n");
		} else if (strcmp(share.GetString("name", ""), argv[2]) == 0)
			id = share.GetInt32("id", -1);
	}

	if (list)
		return 0;

	if (id < 0) {
		fprintf(stderr, "there is no share \"%s\"\n", argv[2]);
		return 1;
	}

	request = BMessage(strcmp(argv[1], "mount") == 0
		? kMountNetworkShare : kUnmountNetworkShare);
	request.AddInt32("id", id);

	BMessage reply;
	status = mountServer.SendMessage(&request, &reply, kTimeout, kTimeout);
	if (status == B_OK)
		status = reply.GetInt32("error", B_ERROR);

	if (status != B_OK) {
		fprintf(stderr, "%s: %s\n", argv[2], strerror(status));
		return 1;
	}

	return 0;
}
