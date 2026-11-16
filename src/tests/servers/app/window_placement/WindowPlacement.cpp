/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


/*!	Opens a window where it is told to, as a program does that puts its
	window back where it was last, and prints the window's frame whenever it
	changes: for trying how windows follow monitors that come and go.

	WindowPlacement [--hidden] [--workspace <index>] [--title <title>]
		<left> <top> <width> <height>
	WindowPlacement --activate <index>

	With --hidden, the window is shown only when asked to by scripting, e.g.
	hey WindowPlacement set Hidden of Window 0 to false. With --workspace, it
	is on that workspace only, counted from 0. --activate switches to a
	workspace.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <View.h>
#include <Window.h>


class PlacementWindow : public BWindow {
public:
	PlacementWindow(BRect frame, const char* title)
		:
		BWindow(frame, title, B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
			B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE)
	{
		BView* view = new BView(Bounds(), "background", B_FOLLOW_ALL, 0);
		view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		AddChild(view);
		_Print("created");
	}

	virtual void FrameMoved(BPoint where)
	{
		_Print("moved");
	}

	virtual void FrameResized(float width, float height)
	{
		_Print("resized");
	}

private:
	void _Print(const char* what)
	{
		BRect frame = Frame();
		printf("%s %s %g %g %g %g\n", Title(), what, frame.left, frame.top,
			frame.right, frame.bottom);
		fflush(stdout);
	}
};


int
main(int argc, char** argv)
{
	bool hidden = false;
	int32 workspace = -1;
	const char* title = "Placement";
	int first = 1;
	while (first < argc && argv[first][0] == '-' && argv[first][1] == '-') {
		if (strcmp(argv[first], "--hidden") == 0)
			hidden = true;
		else if (strcmp(argv[first], "--title") == 0 && first + 1 < argc)
			title = argv[++first];
		else if (strcmp(argv[first], "--workspace") == 0 && first + 1 < argc)
			workspace = atoi(argv[++first]);
		else if (strcmp(argv[first], "--activate") == 0 && first + 1 < argc) {
			BApplication app("application/x-vnd.vos-WindowPlacement");
			activate_workspace(atoi(argv[first + 1]));
			return 0;
		} else if (strcmp(argv[first], "--") == 0) {
			first++;
			break;
		}
		first++;
	}
	if (argc - first != 4) {
		fprintf(stderr, "usage: %s [--hidden] [--workspace <index>] "
			"[--title <title>] <left> <top> <width> <height>\n"
			"       %s --activate <index>\n", argv[0], argv[0]);
		return 1;
	}

	float left = atof(argv[first]);
	float top = atof(argv[first + 1]);
	BRect frame(left, top, left + atof(argv[first + 2]) - 1,
		top + atof(argv[first + 3]) - 1);

	BApplication app("application/x-vnd.vos-WindowPlacement");
	BWindow* window = new PlacementWindow(frame, title);
	if (workspace >= 0)
		window->SetWorkspaces(1UL << workspace);
	if (hidden) {
		// shown later, by scripting; Hide() keeps it hidden after Show()
		window->Hide();
	}
	window->Show();
	app.Run();
	return 0;
}
