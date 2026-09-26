/*
 * Copyright 2008-2011, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <Alert.h>
#include <Application.h>
#include <InterfacePrivate.h>
#include <Message.h>
#include <Screen.h>

#include "ScreenMode.h"


enum {
	kOptionDisplay = 256,
	kOptionScale,
	kOptionPosition,
	kOptionEnable,
	kOptionDisable,
	kOptionPrimary,
	kOptionDisplayMode,
	kOptionZoomToDisplay,
};

static struct option const kLongOptions[] = {
	{"fall-back", no_argument, 0, 'f'},
	{"dont-confirm", no_argument, 0, 'q'},
	{"modeline", no_argument, 0, 'm'},
	{"short", no_argument, 0, 's'},
	{"list", no_argument, 0, 'l'},
	{"help", no_argument, 0, 'h'},
	{"brightness", required_argument, 0, 'b'},
	{"get-brightness", no_argument, 0, 'B'},
	{"displays", no_argument, 0, 'd'},
	{"display", required_argument, 0, kOptionDisplay},
	{"scale", required_argument, 0, kOptionScale},
	{"position", required_argument, 0, kOptionPosition},
	{"enable", no_argument, 0, kOptionEnable},
	{"disable", no_argument, 0, kOptionDisable},
	{"primary", no_argument, 0, kOptionPrimary},
	{"display-mode", required_argument, 0, kOptionDisplayMode},
	{"zoom-to-display", required_argument, 0, kOptionZoomToDisplay},
	{NULL}
};

extern const char *__progname;
static const char *kProgramName = __progname;


static color_space
color_space_for_depth(int32 depth)
{
	switch (depth) {
		case 8:
			return B_CMAP8;
		case 15:
			return B_RGB15;
		case 16:
			return B_RGB16;
		case 24:
			return B_RGB24;
		case 32:
		default:
			return B_RGB32;
	}
}


static void
print_mode(const screen_mode& mode, bool shortOutput)
{
	const char* format
		= shortOutput ? "%ld %ld %ld %g\n" : "%ld %ld, %ld bits, %g Hz\n";
	printf(format, mode.width, mode.height, mode.BitsPerPixel(), mode.refresh);
}


static void
print_mode(const display_mode& displayMode, const screen_mode& mode)
{
	const display_timing& timing = displayMode.timing;

	printf("%" B_PRIu32 "  %u %u %u %u  %u %u %u %u ", timing.pixel_clock / 1000,
		timing.h_display, timing.h_sync_start, timing.h_sync_end,
		timing.h_total, timing.v_display, timing.v_sync_start,
		timing.v_sync_end, timing.v_total);

	// TODO: more flags?
	if ((timing.flags & B_POSITIVE_HSYNC) != 0)
		printf(" +HSync");
	if ((timing.flags & B_POSITIVE_VSYNC) != 0)
		printf(" +VSync");
	if ((timing.flags & B_TIMING_INTERLACED) != 0)
		printf(" Interlace");
	printf(" %" B_PRId32 "\n", mode.BitsPerPixel());
}


static void
print_displays(bool shortOutput)
{
	BMessage layout;
	status_t status = BPrivate::get_display_layout(layout);
	if (status != B_OK) {
		fprintf(stderr, "%s: Could not get the display layout: %s\n",
			kProgramName, strerror(status));
		exit(1);
	}

	bool hasLayout;
	if (layout.FindBool("has layout", &hasLayout) != B_OK)
		hasLayout = false;
	BRect frame;
	layout.FindRect("screen frame", &frame);
	if (!shortOutput) {
		printf("Desktop: %g x %g%s\n", frame.Width() + 1, frame.Height() + 1,
			hasLayout ? "" : " (the driver drives one display, unscaled)");
	}

	BMessage display;
	for (int32 i = 0; layout.FindMessage("display", i, &display) == B_OK;
			i++) {
		int32 id, scale, width, height, nativeWidth, nativeHeight;
		float refresh, nativeRefresh, widthCM, heightCM;
		const char* name;
		const char* monitor;
		const char* vendor;
		bool enabled, connected, primary;
		BRect displayFrame;
		display.FindInt32("id", &id);
		display.FindString("name", &name);
		display.FindString("monitor", &monitor);
		display.FindString("vendor", &vendor);
		display.FindBool("enabled", &enabled);
		display.FindBool("connected", &connected);
		display.FindBool("primary", &primary);
		display.FindRect("frame", &displayFrame);
		display.FindInt32("scale", &scale);
		display.FindInt32("mode width", &width);
		display.FindInt32("mode height", &height);
		display.FindFloat("mode refresh", &refresh);
		display.FindInt32("native width", &nativeWidth);
		display.FindInt32("native height", &nativeHeight);
		display.FindFloat("native refresh", &nativeRefresh);
		display.FindFloat("width cm", &widthCM);
		display.FindFloat("height cm", &heightCM);

		if (shortOutput) {
			printf("%" B_PRId32 " %s %d %d %g %g %" B_PRId32 " %" B_PRId32
				" %" B_PRId32 " %g %d\n", i + 1, name, enabled, connected,
				displayFrame.left, displayFrame.top, scale, width, height,
				refresh, primary);
			continue;
		}

		printf("%" B_PRId32 ": %s%s%s%s\n", i + 1, name,
			monitor[0] != '\0' ? " - " : "", monitor,
			primary ? " (main display)" : "");
		printf("   %s%s, %s\n", connected ? "connected" : "disconnected",
			enabled ? "" : ", off", vendor);
		if (enabled) {
			printf("   mode %" B_PRId32 " x %" B_PRId32 " at %g Hz, native %"
				B_PRId32 " x %" B_PRId32 " at %g Hz\n", width, height, refresh,
				nativeWidth, nativeHeight, nativeRefresh);
			printf("   scale %" B_PRId32 "%%: %g x %g at %g, %g\n", scale,
				displayFrame.Width() + 1, displayFrame.Height() + 1,
				displayFrame.left, displayFrame.top);
		}
		if (widthCM > 0) {
			printf("   %.0f x %.0f cm, %.0f dpi\n", widthCM, heightCM,
				nativeWidth / (widthCM / 2.54f));
		}
		BMessage mode;
		int32 count = 0;
		while (display.FindMessage("modes", count, &mode) == B_OK)
			count++;
		printf("   %" B_PRId32 " modes\n", count);
	}

	bool zoomToDisplay;
	if (!shortOutput && layout.FindBool("zoom to display", &zoomToDisplay)
			== B_OK) {
		printf("Windows maximize to %s.\n",
			zoomToDisplay ? "the display they are on" : "the whole desktop");
	}
}


static int32
find_display(const BMessage& layout, const char* which)
{
	BMessage display;
	for (int32 i = 0; layout.FindMessage("display", i, &display) == B_OK;
			i++) {
		int32 id;
		const char* name;
		display.FindInt32("id", &id);
		display.FindString("name", &name);
		char number[16];
		snprintf(number, sizeof(number), "%" B_PRId32, i + 1);
		if (strcasecmp(name, which) == 0 || strcmp(number, which) == 0)
			return id;
	}
	return -1;
}


static void
usage(int status)
{
	fprintf(stderr,
		"Usage: %s [options] <mode>\n"
		"Sets the specified screen mode. When no screen mode has been chosen,\n"
		"the current one is printed. <mode> takes the form: <width> <height>\n"
		"<depth> <refresh-rate>, or <width>x<height>, etc.\n"
		"      --fall-back\tchanges to the standard fallback mode, and "
			"displays a\n"
		"\t\t\tnotification requester.\n"
		"  -s  --short\t\twhen no mode is given the current screen mode or\n\n"
			"\t\t\tthe screen brightness is printed in short form.\n"
		"  -l  --list\t\tdisplay a list of the available modes.\n"
		"  -q  --dont-confirm\tdo not confirm the mode after setting it.\n"
		"  -b  --brightness f\tset brightness (range 0 to 1).\n"
		"  -b  --brightness +/-f\tchange brightness by given amount.\n"
		"  -B  --get-brightness\tprint the current brightness to stdout.\n"
		"\t\t\tinstead of the screen mode\n"
		"  -m  --modeline\taccept and print X-style modeline modes:\n"
		"\t\t\t  <pclk> <h-display> <h-sync-start> <h-sync-end> <h-total>\n"
		"\t\t\t  <v-disp> <v-sync-start> <v-sync-end> <v-total> [flags] "
			"[depth]\n"
		"\t\t\t(supported flags are: +/-HSync, +/-VSync, Interlace)\n"
		"  -d  --displays\tlist the displays: their arrangement, scale and "
			"modes.\n"
		"      --display <n|name>\tthe display the following options change\n"
		"\t\t\t(its number in the list, or its connector like DP-2):\n"
		"      --scale <percent>\t100, 125, 150, 175, 200, 225 or 250\n"
		"      --position <x> <y>\twhere its top left corner goes\n"
		"      --display-mode <w>x<h>[@<hz>]\n"
		"      --enable, --disable, --primary\n"
		"      --zoom-to-display on|off\n"
		"\t\t\twhether maximizing a window fills only the display it "
			"is on\n",
		kProgramName);

	exit(status);
}


int
main(int argc, char** argv)
{
	bool fallbackMode = false;
	bool setMode = false;
	bool shortOutput = false;
	bool listModes = false;
	bool modeLine = false;
	bool getBrightness = false;
	bool confirm = true;
	int width = -1;
	int height = -1;
	int depth = -1;
	float refresh = -1;
	float brightness = std::nanf("0");
	bool relativeBrightness = false;
	display_mode mode;
	bool listDisplays = false;
	const char* displayName = NULL;
	BMessage displayRequest;
	bool changeDisplay = false;
	int zoomToDisplay = -1;

	// TODO: add a possibility to set a virtual screen size in addition to
	// the display resolution!

	int c;
	while ((c = getopt_long(argc, argv, "shlfqmb:Bd", kLongOptions, NULL)) != -1) {
		switch (c) {
			case 0:
				break;
			case 'd':
				listDisplays = true;
				break;
			case kOptionDisplay:
				displayName = optarg;
				break;
			case kOptionScale:
				displayRequest.AddInt32("scale", strtol(optarg, NULL, 0));
				changeDisplay = true;
				break;
			case kOptionPosition:
			{
				if (optind >= argc)
					usage(1);
				BPoint origin(strtol(optarg, NULL, 0),
					strtol(argv[optind++], NULL, 0));
				displayRequest.AddPoint("origin", origin);
				changeDisplay = true;
				break;
			}
			case kOptionEnable:
				displayRequest.AddBool("enabled", true);
				changeDisplay = true;
				break;
			case kOptionDisable:
				displayRequest.AddBool("enabled", false);
				changeDisplay = true;
				break;
			case kOptionPrimary:
				displayRequest.AddBool("primary", true);
				changeDisplay = true;
				break;
			case kOptionDisplayMode:
			{
				int modeWidth, modeHeight;
				float modeRefresh = 0;
				int parsed = sscanf(optarg, "%dx%d@%f", &modeWidth,
					&modeHeight, &modeRefresh);
				if (parsed < 2)
					usage(1);
				displayRequest.AddInt32("mode width", modeWidth);
				displayRequest.AddInt32("mode height", modeHeight);
				if (parsed == 3)
					displayRequest.AddFloat("mode refresh", modeRefresh);
				changeDisplay = true;
				break;
			}
			case kOptionZoomToDisplay:
				if (!strcasecmp(optarg, "on") || !strcasecmp(optarg, "yes")
					|| !strcmp(optarg, "1"))
					zoomToDisplay = 1;
				else if (!strcasecmp(optarg, "off")
					|| !strcasecmp(optarg, "no") || !strcmp(optarg, "0"))
					zoomToDisplay = 0;
				else
					usage(1);
				break;
			case 'f':
				fallbackMode = true;
				setMode = true;
				confirm = false;
				break;
			case 's':
				shortOutput = true;
				break;
			case 'l':
				listModes = true;
				break;
			case 'm':
				modeLine = true;
				break;
			case 'q':
				confirm = false;
				break;
			case 'b':
				if (optarg[0] == '+' || optarg[0] == '-')
					relativeBrightness = true;
				brightness = atof(optarg);
				break;
			case 'B':
				getBrightness = true;
				break;
			case 'h':
				usage(0);
				break;
			default:
				usage(1);
				break;
		}
	}

	if (argc - optind > 0) {
		int depthIndex = -1;

		// arguments to specify the mode are following

		if (!modeLine) {
			int parsed = sscanf(argv[optind], "%dx%dx%d", &width, &height,
				&depth);
			if (parsed == 2)
				depthIndex = optind + 1;
			else if (parsed == 1) {
				if (argc - optind > 1) {
					height = strtol(argv[optind + 1], NULL, 0);
					depthIndex = optind + 2;
				} else
					usage(1);
			} else if (parsed != 3)
				usage(1);

			if (depthIndex > 0 && depthIndex < argc)
				depth = strtol(argv[depthIndex], NULL, 0);
			if (depthIndex + 1 < argc)
				refresh = strtod(argv[depthIndex + 1], NULL);
		} else {
			// parse mode line
			if (argc - optind < 9)
				usage(1);

			mode.timing.pixel_clock = strtol(argv[optind], NULL, 0) * 1000;
			mode.timing.h_display = strtol(argv[optind + 1], NULL, 0);
			mode.timing.h_sync_start = strtol(argv[optind + 2], NULL, 0);
			mode.timing.h_sync_end = strtol(argv[optind + 3], NULL, 0);
			mode.timing.h_total = strtol(argv[optind + 4], NULL, 0);
			mode.timing.v_display = strtol(argv[optind + 5], NULL, 0);
			mode.timing.v_sync_start = strtol(argv[optind + 6], NULL, 0);
			mode.timing.v_sync_end = strtol(argv[optind + 7], NULL, 0);
			mode.timing.v_total = strtol(argv[optind + 8], NULL, 0);
			mode.timing.flags = 0;
			mode.space = B_RGB32;

			int i = optind + 9;
			while (i < argc) {
				if (!strcasecmp(argv[i], "+HSync"))
					mode.timing.flags |= B_POSITIVE_HSYNC;
				else if (!strcasecmp(argv[i], "+VSync"))
					mode.timing.flags |= B_POSITIVE_VSYNC;
				else if (!strcasecmp(argv[i], "Interlace"))
					mode.timing.flags |= B_TIMING_INTERLACED;
				else if (!strcasecmp(argv[i], "-VSync")
					|| !strcasecmp(argv[i], "-HSync")) {
					// okay, but nothing to do
				} else if (isdigit(argv[i][0]) && i + 1 == argc) {
					// bits per pixel
					mode.space
						= color_space_for_depth(strtoul(argv[i], NULL, 0));
				} else {
					fprintf(stderr, "Unknown flag: %s\n", argv[i]);
					exit(1);
				}

				i++;
			}

			mode.virtual_width = mode.timing.h_display;
			mode.virtual_height = mode.timing.v_display;
			mode.h_display_start = 0;
			mode.v_display_start = 0;
		}

		setMode = true;
	}

	BApplication application("application/x-vnd.vos-screenmode");

	if (zoomToDisplay >= 0)
		BPrivate::set_zoom_to_display(zoomToDisplay == 1);

	if (changeDisplay) {
		if (displayName == NULL) {
			fprintf(stderr, "%s: --display says which display to change\n",
				kProgramName);
			return 1;
		}
		BMessage layout;
		status_t status = BPrivate::get_display_layout(layout);
		if (status != B_OK) {
			fprintf(stderr, "%s: Could not get the display layout: %s\n",
				kProgramName, strerror(status));
			return 1;
		}
		int32 id = find_display(layout, displayName);
		if (id < 0) {
			fprintf(stderr, "%s: There is no display \"%s\"\n", kProgramName,
				displayName);
			return 1;
		}
		displayRequest.AddInt32("id", id);
		BMessage request;
		request.AddMessage("display", &displayRequest);
		status = BPrivate::set_display_layout(request);
		if (status != B_OK) {
			fprintf(stderr, "%s: Could not change the display: %s\n",
				kProgramName, strerror(status));
			return 1;
		}
		listDisplays = true;
	}

	if (listDisplays) {
		print_displays(shortOutput);
		return 0;
	}
	if (zoomToDisplay >= 0 && !setMode && !listModes && !getBrightness)
		return 0;

	ScreenMode screenMode(NULL);
	screen_mode currentMode;
	screenMode.Get(currentMode);
	if (!isnan(brightness)) {
		BScreen screen;
		if (relativeBrightness) {
			float previousBrightness;
			screen.GetBrightness(&previousBrightness);
			brightness = previousBrightness + brightness;

			// Clamp to min/max values
			if (brightness < 0.f)
				brightness = 0.f;

			if (brightness > 1.f)
				brightness = 1.f;
		}

		if (brightness < 0.f || brightness > 1.f)
			printf("Brightness %f is out of range\n", brightness);
		screen.SetBrightness(brightness);
	}

	if (listModes) {
		// List all reported modes
		if (!shortOutput)
			printf("Available screen modes:\n");

		for (int index = 0; index < screenMode.CountModes(); index++) {
			if (modeLine) {
				print_mode(screenMode.DisplayModeAt(index),
					screenMode.ModeAt(index));
			} else
				print_mode(screenMode.ModeAt(index), shortOutput);
		}

		return 0;
	}

	if (!setMode && !getBrightness) {
		// Just print the current mode
		if (modeLine) {
			display_mode mode;
			screenMode.Get(mode);
			print_mode(mode, currentMode);
		} else {
			if (!shortOutput)
				printf("Resolution: ");
			print_mode(currentMode, shortOutput);
		}
		return 0;
	}

	status_t status;

	if (getBrightness) {
		float brightnessToPrint = std::nanf("0");
		BScreen screen;
		status = screen.GetBrightness(&brightnessToPrint);
		if (status != B_OK) {
			fprintf(stderr, "Error retrieving brightness: %s\n", strerror(status));
			return 1;
		}
		if (!shortOutput)
			printf("Brightness: ");
		printf("%f\n", brightnessToPrint);
		return 0;
	}

	screen_mode newMode = currentMode;

	if (fallbackMode) {
		if (currentMode.width == 800 && currentMode.height == 600) {
			newMode.width = 640;
			newMode.height = 480;
			newMode.space = B_CMAP8;
			newMode.refresh = 60;
		} else {
			newMode.width = 800;
			newMode.height = 600;
			newMode.space = B_RGB16;
			newMode.refresh = 60;
		}
	} else if (modeLine) {
		display_mode currentDisplayMode;
		if (screenMode.Get(currentDisplayMode) == B_OK)
			mode.flags = currentDisplayMode.flags;
	} else {
		newMode.width = width;
		newMode.height = height;

		if (depth != -1)
			newMode.space = color_space_for_depth(depth);
		else
			newMode.space = B_RGB32;

		if (refresh > 0)
			newMode.refresh = refresh;
		else
			newMode.refresh = 60;
	}

	if (modeLine)
		status = screenMode.Set(mode);
	else
		status = screenMode.Set(newMode);

	if (status == B_OK) {
		if (confirm) {
			printf("Is this mode okay (Y/n - will revert after 10 seconds)? ");
			fflush(stdout);

			int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
			fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

			bigtime_t end = system_time() + 10000000LL;
			int c = 'n';
			while (system_time() < end) {
				c = getchar();
				if (c != -1)
					break;

				snooze(10000);
			}

			if (c != '\n' && tolower(c) != 'y')
				screenMode.Revert();
		}
	} else {
		fprintf(stderr,
			"%s: Could not set screen mode "
			"%" B_PRId32 "x%" B_PRId32 "x%" B_PRId32 ": "
			"%s\n",
				kProgramName,
				newMode.width, newMode.height, newMode.BitsPerPixel(),
				strerror(status));
		return 1;
	}

	if (fallbackMode) {
		// display notification requester
		BAlert* alert = new BAlert("screenmode",
			"You have used the shortcut <Shift><Command><Ctrl><Escape> to "
			"reset the screen mode to a safe fallback.", "Keep", "Revert");
		alert->SetShortcut(1, B_ESCAPE);
		if (alert->Go() == 1)
			screenMode.Revert();
	}

	return 0;
}
