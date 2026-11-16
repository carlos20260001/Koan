/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	direct_window_burst_test [windows] [slow]: opens BDirectWindows one after the
	other from the application thread, as a program restoring its windows
	does, and checks that every one gets connected and keeps its connection.

	The direct daemon of a new window maps the frame buffer before its first
	DirectConnected(), while app_server waits for it - half a second at most,
	after which app_server drops the window's direct connection for good
	("killed for a problem in DirectConnected()"). It used to take the
	application's app_server link lock for that, which the application
	thread holds while it creates the next window.

	With "slow", DirectConnected() takes longer than app_server waits, so
	app_server gives up on every window; quitting them must still work.
*/


#include <Application.h>
#include <DirectWindow.h>
#include <OS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int32 sConnected;
static int32 sStarted;
static bool sSlow;


class Window : public BDirectWindow {
public:
	Window(BRect frame)
		:
		BDirectWindow(frame, "direct_window_burst_test", B_TITLED_WINDOW,
			B_NOT_RESIZABLE),
		fStarted(false)
	{
	}

	void DirectConnected(direct_buffer_info* info)
	{
		if ((info->buffer_state & B_DIRECT_MODE_MASK) == B_DIRECT_START
			&& !fStarted) {
			fStarted = true;
			atomic_add(&sStarted, 1);
		}
		if (info->bits != NULL)
			atomic_add(&sConnected, 1);
		// some work, as a program drawing into the screen does
		snooze(sSlow ? 800000 : 2000);
	}

	bool fStarted;
};


static int sCount;
static int sResult = 1;


static status_t
run_test(void*)
{
	int count = sCount;
	Window* windows[64];
	bigtime_t start = system_time();
	for (int i = 0; i < count && i < 64; i++) {
		BRect frame(40 + i * 30, 40 + i * 30, 340 + i * 30, 240 + i * 30);
		windows[i] = new Window(frame);
		windows[i]->Show();
	}
	bigtime_t created = system_time() - start;
	snooze(3000000);

	int started = 0;
	for (int i = 0; i < count && i < 64; i++) {
		if (windows[i]->fStarted)
			started++;
	}
	printf("%d windows created in %.0f ms: %d got B_DIRECT_START, %d "
		"DirectConnected() calls with bits\n", count, created / 1000.0,
		started, (int)atomic_get(&sConnected));

	for (int i = 0; i < count && i < 64; i++)
		windows[i]->PostMessage(B_QUIT_REQUESTED);
	sResult = started == count || sSlow ? 0 : 1;
	printf("%s\n", sResult == 0 ? "PASS" : "FAIL");
	be_app->PostMessage(B_QUIT_REQUESTED);
	return B_OK;
}


int
main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	BApplication app("application/x-vnd.vos-direct-window-burst-test");
	sCount = argc > 1 ? atoi(argv[1]) : 8;
	sSlow = argc > 2 && strcmp(argv[2], "slow") == 0;

	if (!BDirectWindow::SupportsWindowMode()) {
		printf("no direct window support\n");
		return 2;
	}

	thread_id thread = spawn_thread(run_test, "burst", B_NORMAL_PRIORITY,
		NULL);
	resume_thread(thread);
	app.Run();
	status_t ignored;
	wait_for_thread(thread, &ignored);
	return sResult;
}
