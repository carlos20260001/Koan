/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	wheel_log [seconds]: a window that prints the B_MOUSE_WHEEL_CHANGED
	messages it gets (whole and precise deltas, phase, device), then a
	summary. Scroll over it with a touchpad or a mouse wheel.
*/


#include <Application.h>
#include <Input.h>
#include <MessageRunner.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>


static int32 sMessages, sPrecise, sBegan, sChanged, sEnded;
static float sWholeY, sPreciseY;


class LogView : public BView {
public:
	LogView(BRect frame)
		:
		BView(frame, "log", B_FOLLOW_ALL, B_WILL_DRAW)
	{
		SetViewColor(200, 220, 255);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != B_MOUSE_WHEEL_CHANGED) {
			BView::MessageReceived(message);
			return;
		}

		float x = 0, y = 0, px = 0, py = 0;
		int32 phase = -1, subtype = -1;
		message->FindFloat("be:wheel_delta_x", &x);
		message->FindFloat("be:wheel_delta_y", &y);
		bool precise = message->FindFloat("be:wheel_precise_delta_x", &px)
				== B_OK
			&& message->FindFloat("be:wheel_precise_delta_y", &py) == B_OK;
		message->FindInt32("be:wheel_phase", &phase);
		message->FindInt32("be:device_subtype", &subtype);
		printf("wheel %g,%g precise %s%.3f,%.3f phase %d device %d\n", x, y,
			precise ? "" : "(none) ", px, py, (int)phase, (int)subtype);

		sMessages++;
		sWholeY += y;
		if (precise) {
			sPrecise++;
			sPreciseY += py;
		}
		if (phase == B_WHEEL_PHASE_BEGAN)
			sBegan++;
		else if (phase == B_WHEEL_PHASE_CHANGED)
			sChanged++;
		else if (phase == B_WHEEL_PHASE_ENDED)
			sEnded++;
	}
};


int
main(int argc, char** argv)
{
	BApplication app("application/x-vnd.vos-wheel-log");
	bigtime_t duration = (argc > 1 ? atoi(argv[1]) : 30) * 1000000LL;

	BRect frame(100, 100, 1100, 800);
	BWindow* window = new BWindow(frame, "Scroll here (wheel_log)",
		B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE);
	LogView* view = new LogView(window->Bounds());
	window->AddChild(view);
	window->Show();
	if (window->Lock()) {
		view->MakeFocus();
		window->Activate();
		window->Unlock();
	}

	BMessage quit(B_QUIT_REQUESTED);
	BMessageRunner runner(be_app_messenger, &quit, duration, 1);
	app.Run();

	printf("messages %d, with precise deltas %d; phases began %d changed %d "
		"ended %d; whole y %g, precise y %.3f\n", (int)sMessages,
		(int)sPrecise, (int)sBegan, (int)sChanged, (int)sEnded, sWholeY,
		sPreciseY);
	return 0;
}
