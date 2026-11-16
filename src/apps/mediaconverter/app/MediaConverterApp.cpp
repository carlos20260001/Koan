/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "MediaConverterApp.h"

#include <AboutWindow.h>
#include <Catalog.h>
#include <Entry.h>
#include <Path.h>

#include <stdio.h>
#include <string.h>

extern "C" {
#include <libavutil/log.h>
}

#include "JobProcess.h"
#include "MediaConverterWindow.h"
#include "Messages.h"
#include "NvencEncoder.h"
#include "Presets.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MediaConverter"


const char* kAppSignature = "application/x-vnd.vos-MediaConverter";


MediaConverterApp::MediaConverterApp()
	:
	BApplication(kAppSignature),
	fWindow(NULL),
	fPendingRefs(B_REFS_RECEIVED)
{
	av_log_set_level(AV_LOG_ERROR);
}


MediaConverterApp::~MediaConverterApp()
{
}


void
MediaConverterApp::ReadyToRun()
{
	fWindow = new MediaConverterWindow();
	if (!fPendingRefs.IsEmpty())
		fWindow->PostMessage(&fPendingRefs);
	_PostPending();
	fWindow->Show();
}


void
MediaConverterApp::RefsReceived(BMessage* message)
{
	if (fWindow == NULL) {
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++)
			fPendingRefs.AddRef("refs", &ref);
		return;
	}
	fWindow->PostMessage(message);
}


void
MediaConverterApp::ArgvReceived(int32 argc, char** argv)
{
	// MediaConverter [--preset <name>] [--convert] [files]
	BMessage refs(B_REFS_RECEIVED);
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--preset") == 0 && i + 1 < argc) {
			int32 index = Presets::IndexOf(argv[++i]);
			if (index < 0) {
				fprintf(stderr, "MediaConverter: no preset \"%s\"\n", argv[i]);
				continue;
			}
			BMessage preset(kMsgPresetChosen);
			preset.AddInt32("value", index);
			fPending.AddItem(new BMessage(preset));
			continue;
		}
		if (strcmp(argv[i], "--convert") == 0) {
			fPending.AddItem(new BMessage(kMsgConvert));
			continue;
		}
		BEntry entry(argv[i], true);
		entry_ref ref;
		if (entry.Exists() && entry.GetRef(&ref) == B_OK)
			refs.AddRef("refs", &ref);
	}
	if (!refs.IsEmpty())
		RefsReceived(&refs);
	if (fWindow != NULL)
		_PostPending();
}


void
MediaConverterApp::_PostPending()
{
	// the files first: a conversion asked for is of them
	for (int32 i = 0; i < fPending.CountItems(); i++) {
		BMessage* message = (BMessage*)fPending.ItemAt(i);
		if (message->what == kMsgConvert)
			continue;
		fWindow->PostMessage(message);
	}
	for (int32 i = 0; i < fPending.CountItems(); i++) {
		BMessage* message = (BMessage*)fPending.ItemAt(i);
		if (message->what == kMsgConvert)
			fWindow->PostMessage(message);
		delete message;
	}
	fPending.MakeEmpty();
}


bool
MediaConverterApp::QuitRequested()
{
	if (fWindow != NULL && fWindow->Lock()) {
		bool quit = fWindow->QuitRequested();
		if (quit)
			fWindow->Quit();
		else
			fWindow->Unlock();
		if (!quit)
			return false;
		fWindow = NULL;
	}
	return true;
}


void
MediaConverterApp::AboutRequested()
{
	BAboutWindow* about = new BAboutWindow(
		B_TRANSLATE_SYSTEM_NAME("MediaConverter"), kAppSignature);
	about->AddDescription(B_TRANSLATE("Converts video and audio files, on "
		"the graphics card's video encoder when it has one."));
	const char* authors[] = {
		"air/OS",
		"Jun Suzuki (the original MediaConverter)",
		"Stephan Aßmus",
		NULL
	};
	about->AddCopyright(2026, "air/OS");
	about->AddAuthors(authors);
	about->AddText(B_TRANSLATE("Uses FFmpeg, x264, x265, SVT-AV1, libva and "
		"Mesa."));
	about->Show();
}


int
main(int argc, char** argv)
{
	// NvencEncoder::SelfTest() runs the NVENC test in a process of its own
	if (argc == 3 && strcmp(argv[1], "--nvenc-self-test") == 0)
		return NvencEncoder::SelfTestMain(argv[2]);
	// JobProcess runs each conversion in one of these
	if (argc == 2 && strcmp(argv[1], "--worker") == 0)
		return JobProcess::WorkerMain();

	MediaConverterApp app;
	app.Run();
	return 0;
}
