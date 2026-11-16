/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


/*!	A stand-in web browser for testing what Terminal opens: made the
	preferred application for http and https addresses, it appends each
	address it is asked to open to the file named in $URL_CATCHER_LOG, or
	/var/tmp/url-catcher.log, and quits. Registering it needs a
	writable MIME database (not a live CD):

		addattr -t mime BEOS:APP_SIG application/x-vnd.vos-URLCatcher \
			URLCatcher
		setmime -set application/x-vnd.Be.URL.https \
			-preferredAppSig application/x-vnd.vos-URLCatcher
*/


#include <stdio.h>
#include <stdlib.h>

#include <Application.h>


class URLCatcher : public BApplication {
public:
	URLCatcher()
		:
		BApplication("application/x-vnd.vos-URLCatcher")
	{
	}

	virtual void ArgvReceived(int32 argc, char** argv)
	{
		const char* path = getenv("URL_CATCHER_LOG");
		FILE* log = fopen(path != NULL ? path : "/var/tmp/url-catcher.log",
			"a");
		if (log == NULL)
			return;
		for (int32 i = 1; i < argc; i++)
			fprintf(log, "%s\n", argv[i]);
		fclose(log);
	}

	virtual void ReadyToRun()
	{
		PostMessage(B_QUIT_REQUESTED);
	}
};


int
main()
{
	URLCatcher catcher;
	catcher.Run();
	return 0;
}
