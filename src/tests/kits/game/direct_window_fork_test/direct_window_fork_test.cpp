/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	A BDirectWindow connects, which maps the frame buffer into this team, and
	then the team forks. Mode 0: the child exits at once; 1: it reads and
	writes the frame buffer first; 2: it execs /bin/true; 3: it only reads.
	A child that touched the inherited mapping used to crash: fork() didn't
	map the device memory. (Summit's web process launcher hung the X399
	after forking from a team with a connected direct window.)
*/


#include <Application.h>
#include <DirectWindow.h>
#include <OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static sem_id sConnected;
static volatile uint32* sBits;

class Window : public BDirectWindow {
public:
	Window() : BDirectWindow(BRect(100, 100, 300, 300), "direct_window_fork_test",
		B_TITLED_WINDOW, B_NOT_RESIZABLE) {}
	void DirectConnected(direct_buffer_info* info)
	{
		if ((info->buffer_state & B_DIRECT_MODE_MASK) == B_DIRECT_START) {
			sBits = (volatile uint32*)info->bits;
			release_sem(sConnected);
		}
		BDirectWindow::DirectConnected(info);
	}
};

int main(int argc, char** argv)
{
	int mode = argc > 1 ? atoi(argv[1]) : 0;
	BApplication app("application/x-vnd.vos-direct_window_fork_test");
	sConnected = create_sem(0, "connected");
	Window* window = new Window;
	window->Show();
	if (acquire_sem_etc(sConnected, 1, B_RELATIVE_TIMEOUT, 5000000) != B_OK) {
		printf("no direct connection\n");
		return 2;
	}
	area_info info;
	get_area_info(area_for((void*)sBits), &info);
	printf("frame buffer at %p in area %d \"%s\", %zu bytes, protection %#x lock %u\n",
		sBits, (int)info.area, info.name, info.size, (unsigned)info.protection,
		(unsigned)info.lock);
	sBits[0] = 0x00ff0000;
	fflush(stdout);

	pid_t child = fork();
	if (child == 0) {
		if (mode == 1 || mode == 3) {
			area_info childInfo;
			status_t status = get_area_info(area_for((void*)sBits), &childInfo);
			printf("child: area %d status %d protection %#x lock %u size %zu ram %zu\n",
				(int)childInfo.area, (int)status, (unsigned)childInfo.protection,
				(unsigned)childInfo.lock, childInfo.size, childInfo.ram_size);
			fflush(stdout);
			uint32 value = sBits[0];
			printf("child: read %#x\n", (unsigned)value);
			fflush(stdout);
			if (mode == 1) {
				sBits[1] = 0x0000ff00;
				printf("child: wrote\n");
				fflush(stdout);
			}
		}
		if (mode == 2)
			execl("/bin/true", "true", (char*)NULL);
		_exit(0);
	}
	int status;
	waitpid(child, &status, 0);
	printf("mode %d: child exited %d\n", mode, WEXITSTATUS(status));
	window->Lock();
	window->Quit();
	return 0;
}
