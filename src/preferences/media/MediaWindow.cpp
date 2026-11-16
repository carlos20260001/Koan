/*
 * Copyright 2003-2026, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Sikosis, Jérôme Duval
 *		yourpalal, Alex Wilson
 */


#include "MediaWindow.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MediaRoster.h>
#include <MessageRunner.h>
#include <LaunchRoster.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <Roster.h>
#include <ScrollView.h>
#include <SeparatorView.h>

#include <MidiSettings.h>

#include "Media.h"
#include "MediaMessages.h"
#include "Pages.h"
#include "Sidebar.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Media Window"


static property_info sProperties[] = {
	{ "Page", { B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"The page shown: output, input, mixer, midi, camera or "
		"device:<name>.", 0, { B_STRING_TYPE }
	},
	{ "Scroll", { B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"How far the page is scrolled down.", 0, { B_FLOAT_TYPE }
	},
	{ 0 }
};


static const char* kMediaServerJob = "x-vnd.vos-media_server";
static const char* kRestartPageKey = "restarting";
static const char* kServerPageKey = "server";


static float
ui_scale()
{
	return std::max(1.0f, be_plain_font->Size() / 12.0f);
}


static BString
soundfont_name()
{
	struct BPrivate::midi_settings settings;
	if (BPrivate::read_midi_settings(&settings) != B_OK
		|| settings.soundfont_file[0] == '\0') {
		return B_TRANSLATE("No SoundFont");
	}
	return BPath(settings.soundfont_file).Leaf();
}


MediaWindow::MediaWindow(BRect frame, const char* page)
	:
	BWindow(frame, B_TRANSLATE_SYSTEM_NAME("Media"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fSidebar(NULL),
	fPageHolder(NULL),
	fPage(NULL),
	fWantedKey(page != NULL && page[0] != '\0' ? page : "output"),
	fRestartButton(NULL),
	fServerRunning(false),
	fWatching(false),
	fRestartThread(-1),
	fRescanRunner(NULL)
{
	_InitWindow();

	status_t status = B_ERROR;
	BMediaRoster* roster = BMediaRoster::Roster(&status);
	fServerRunning = roster != NULL && status == B_OK
		&& BMediaRoster::IsRunning();
	_StartWatching();
	_Rescan(true);
}


MediaWindow::~MediaWindow()
{
	delete fRescanRunner;

	BMediaRoster* roster = BMediaRoster::CurrentRoster();
	if (fWatching && roster != NULL) {
		BMessenger messenger(this);
		roster->StopWatching(messenger, B_MEDIA_SERVER_STARTED);
		roster->StopWatching(messenger, B_MEDIA_SERVER_QUIT);
		roster->StopWatching(messenger, B_MEDIA_FLAVORS_CHANGED);
		roster->StopWatching(messenger, B_MEDIA_NODE_CREATED);
		roster->StopWatching(messenger, B_MEDIA_NODE_DELETED);
		roster->StopWatching(messenger, B_MEDIA_DEFAULT_CHANGED);
	}

	for (int32 i = fSidebar->CountItems() - 1; i >= 0; i--)
		delete fSidebar->RemoveItem(i);
}


bool
MediaWindow::QuitRequested()
{
	if (fRestartThread >= 0) {
		BString text(B_TRANSLATE("Quitting %prefname% now will stop the "
			"restarting of the media services. Flaky or unavailable media "
			"functionality is the likely result."));
		text.ReplaceFirst("%prefname%", B_TRANSLATE_SYSTEM_NAME("Media"));

		BAlert* alert = new BAlert(B_TRANSLATE("Warning!"), text,
			B_TRANSLATE("Cancel"), B_TRANSLATE("Quit anyway"), NULL,
			B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		if (alert->Go() == 0)
			return false;
	}

	BMessage settings;
	settings.AddRect("frame", Frame());
	settings.AddString("page", fPageKey == kServerPageKey
		|| fPageKey == kRestartPageKey ? fWantedKey : fPageKey);
	save_media_settings(settings);

	// The page lets go of the nodes it uses.
	if (fPage != NULL) {
		fPage->RemoveSelf();
		delete fPage;
		fPage = NULL;
	}

	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


BHandler*
MediaWindow::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	BPropertyInfo info(sProperties);
	if (info.FindMatch(message, index, specifier, what, property) >= 0)
		return this;
	return BWindow::ResolveSpecifier(message, index, specifier, what,
		property);
}


status_t
MediaWindow::GetSupportedSuites(BMessage* data)
{
	data->AddString("suites", "suite/vnd.Haiku-media-window");
	BPropertyInfo info(sProperties);
	data->AddFlat("messages", &info);
	return BWindow::GetSupportedSuites(data);
}


bool
MediaWindow::_HandleScripting(BMessage* message)
{
	if (message->what != B_GET_PROPERTY && message->what != B_SET_PROPERTY)
		return false;

	int32 index;
	BMessage specifier;
	int32 form;
	const char* property;
	if (message->GetCurrentSpecifier(&index, &specifier, &form, &property)
			!= B_OK) {
		return false;
	}

	BMessage reply(B_REPLY);
	if (strcmp(property, "Scroll") == 0 && fPage != NULL) {
		float position;
		if (message->what == B_GET_PROPERTY)
			reply.AddFloat("result", fPage->ScrollPosition());
		else if (message->FindFloat("data", &position) == B_OK) {
			fPage->SetScrollPosition(position);
			reply.AddInt32("error", B_OK);
		} else
			reply.AddInt32("error", B_BAD_VALUE);
		message->SendReply(&reply);
		return true;
	}
	if (strcmp(property, "Page") != 0)
		return false;

	if (message->what == B_GET_PROPERTY)
		reply.AddString("result", fPageKey);
	else {
		const char* key;
		if (fRestartThread < 0 && message->FindString("data", &key) == B_OK) {
			_ShowPage(key);
			reply.AddInt32("error", fPageKey == key ? B_OK : B_BAD_VALUE);
		} else
			reply.AddInt32("error", B_BAD_VALUE);
	}
	message->SendReply(&reply);
	return true;
}


void
MediaWindow::MessageReceived(BMessage* message)
{
	if (_HandleScripting(message))
		return;

	switch (message->what) {
		case kMsgSidebarSelection:
		{
			const char* key;
			if (fRestartThread < 0
				&& message->FindString("key", &key) == B_OK) {
				fPendingKey = "";
				_ShowPage(key);
			}
			break;
		}

		case kMsgShowPage:
		{
			const char* key;
			if (fRestartThread < 0
				&& message->FindString("key", &key) == B_OK) {
				_ShowPage(key);
			}
			break;
		}

		case kMsgSetDefault:
			_SetDefault(message);
			break;

		case kMsgSetOutputChannel:
		{
			const media_input* input;
			ssize_t size;
			if (message->FindData("input", B_RAW_TYPE, (const void**)&input,
					&size) == B_OK && size == sizeof(media_input)) {
				BMediaRoster::Roster()->SetAudioOutput(*input);
				_Rescan(false);
			}
			break;
		}

		case kMsgRescan:
			delete fRescanRunner;
			fRescanRunner = NULL;
			_Rescan(false);
			break;

		case kMsgRestartServices:
		{
			if (fRestartThread >= 0)
				break;

			// Let go of the nodes before the servers go away.
			fWantedKey = fPageKey == kServerPageKey ? fWantedKey : fPageKey;
			if (fPage != NULL) {
				fPage->RemoveSelf();
				delete fPage;
			}
			fPage = new MessagePage(kRestartPageKey, GLYPH_WARNING,
				B_TRANSLATE("Restarting media services"),
				B_TRANSLATE("Stopping the media services…"), NULL, NULL,
				&fModel);
			fPageKey = kRestartPageKey;
			fPageHolder->GroupLayout()->AddView(fPage);
			fPage->Rebuild();
			// the devices come back with the services
			for (int32 i = fSidebar->CountItems() - 1; i >= 0; i--)
				delete fSidebar->RemoveItem(i);
			fRestartButton->SetEnabled(false);

			fRestartThread = spawn_thread(&_RestartThread,
				"restart media services", B_NORMAL_PRIORITY,
				new BMessenger(this));
			if (fRestartThread >= 0)
				resume_thread(fRestartThread);
			else
				PostMessage(kMsgRestartDone);
			break;
		}

		case kMsgStartServices:
		{
			if (fRestartThread >= 0)
				break;
			MessagePage* page = dynamic_cast<MessagePage*>(fPage);
			if (page != NULL)
				page->SetText(B_TRANSLATE("Starting the media services…"));
			fRestartThread = spawn_thread(&_RestartThread,
				"start media services", B_NORMAL_PRIORITY,
				new BMessenger(this));
			if (fRestartThread >= 0)
				resume_thread(fRestartThread);
			break;
		}

		case kMsgRestartProgress:
		{
			const char* text;
			MessagePage* page = dynamic_cast<MessagePage*>(fPage);
			if (page != NULL && message->FindString("text", &text) == B_OK)
				page->SetText(text);
			break;
		}

		case kMsgRestartDone:
		{
			if (fRestartThread >= 0) {
				status_t result;
				wait_for_thread(fRestartThread, &result);
				fRestartThread = -1;
			}
			fRestartButton->SetEnabled(true);
			fServerRunning = BMediaRoster::IsRunning();

			status_t status = B_OK;
			message->FindInt32("status", &status);
			if (!fServerRunning) {
				MessagePage* page = dynamic_cast<MessagePage*>(fPage);
				if (fPageKey != kServerPageKey || page == NULL) {
					_ShowServerDown();
					page = dynamic_cast<MessagePage*>(fPage);
				}
				BString text(B_TRANSLATE("The media services could not be "
					"started: %error%."));
				text.ReplaceFirst("%error%", strerror(status != B_OK
					? status : B_ERROR));
				if (page != NULL)
					page->SetText(text);
				break;
			}

			BMediaRoster::Roster();
			_StartWatching();
			if (fPage != NULL) {
				fPage->RemoveSelf();
				delete fPage;
				fPage = NULL;
				fPageKey = "";
			}
			_Rescan(true);
			break;
		}

		case B_MEDIA_SERVER_STARTED:
			fServerRunning = true;
			_StartWatching();
			_ScheduleRescan();
			break;

		case B_MEDIA_SERVER_QUIT:
			if (fRestartThread < 0) {
				fServerRunning = false;
				_ShowServerDown();
			}
			break;

		case B_MEDIA_FLAVORS_CHANGED:
		case B_MEDIA_NODE_CREATED:
		case B_MEDIA_NODE_DELETED:
		case B_MEDIA_DEFAULT_CHANGED:
			_ScheduleRescan();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


// #pragma mark - private


void
MediaWindow::_InitWindow()
{
	float scale = ui_scale();
	float spacing = be_control_look->DefaultItemSpacing();

	fSidebar = new Sidebar();
	BScrollView* sidebarScroller = new BScrollView("sidebar scroller",
		fSidebar, 0, false, false, B_NO_BORDER);

	fRestartButton = new BButton("restart",
		B_TRANSLATE("Restart media services"),
		new BMessage(kMsgRestartServices));
	fRestartButton->SetToolTip(B_TRANSLATE("Stops and starts the media "
		"services, which can help when sound or video stopped working."));

	float sidebarWidth = std::max(ceilf(190 * scale),
		fRestartButton->PreferredSize().width + spacing * 2);
	sidebarScroller->SetExplicitMinSize(BSize(sidebarWidth, 100));
	sidebarScroller->SetExplicitMaxSize(BSize(sidebarWidth, B_SIZE_UNLIMITED));

	BGroupView* column = new BGroupView(B_VERTICAL, 0);
	column->SetViewColor(sidebar_color());
	column->SetLowColor(sidebar_color());
	BLayoutBuilder::Group<>(column)
		.Add(sidebarScroller)
		.AddGroup(B_HORIZONTAL)
			.SetInsets(spacing)
			.Add(fRestartButton)
		.End();

	fPageHolder = new BGroupView(B_VERTICAL, 0);
	fPageHolder->SetExplicitMinSize(BSize(ceilf(400 * scale), 200));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 0)
		.Add(column, 0)
		.Add(new BSeparatorView(B_VERTICAL, B_PLAIN_BORDER))
		.Add(fPageHolder, 1);
}


void
MediaWindow::_StartWatching()
{
	BMediaRoster* roster = BMediaRoster::CurrentRoster();
	if (roster == NULL)
		return;

	BMessenger messenger(this);
	if (!fWatching) {
		// these two the local roster tells
		roster->StartWatching(messenger, B_MEDIA_SERVER_STARTED);
		roster->StartWatching(messenger, B_MEDIA_SERVER_QUIT);
		fWatching = true;
	}

	// The media server keeps these, so a new server needs them again.
	static const int32 kServerNotifications[] = { B_MEDIA_FLAVORS_CHANGED,
		B_MEDIA_NODE_CREATED, B_MEDIA_NODE_DELETED, B_MEDIA_DEFAULT_CHANGED };
	for (size_t i = 0; i < B_COUNT_OF(kServerNotifications); i++) {
		roster->StopWatching(messenger, kServerNotifications[i]);
		roster->StartWatching(messenger, kServerNotifications[i]);
	}
}


void
MediaWindow::_ScheduleRescan()
{
	delete fRescanRunner;
	BMessage message(kMsgRescan);
	fRescanRunner = new BMessageRunner(BMessenger(this), &message, 300000, 1);
}


void
MediaWindow::_Rescan(bool force)
{
	if (fRestartThread >= 0)
		return;

	MediaModel model;
	if (!fServerRunning || model.Scan() != B_OK) {
		_ShowServerDown();
		return;
	}

	bool showPage = fPage == NULL || fPageKey == kServerPageKey
		|| fPageKey == kRestartPageKey;
	if (!force && !showPage && model.Equals(fModel))
		return;

	fModel = model;
	_RebuildSidebar();

	if (!fPendingKey.IsEmpty() && fPageKey == "output"
		&& fModel.DeviceFor(fPendingKey) != NULL) {
		BString key(fPendingKey);
		_ShowPage(key);
		return;
	}

	if (showPage) {
		_ShowPage(fWantedKey);
		return;
	}

	if (fPageKey.StartsWith("device:")) {
		const DeviceEntry* device = fModel.DeviceFor(fPageKey);
		DevicePage* page = dynamic_cast<DevicePage*>(fPage);
		if (device == NULL || page == NULL || !page->Shows(*device)) {
			// The device went away, or its name is another device's now
			// ("HD Audio 2" became "HD Audio").
			_ShowPage("output");
			return;
		}
	}

	fPage->ModelChanged();
	fSidebar->SelectKey(fPageKey);
}


void
MediaWindow::_RebuildSidebar()
{
	for (int32 i = fSidebar->CountItems() - 1; i >= 0; i--)
		delete fSidebar->RemoveItem(i);

	BString none(B_TRANSLATE("None"));
	BString name;

	fSidebar->AddItem(new SidebarItem(B_TRANSLATE("Sound")));
	name = fModel.DefaultName(ROLE_AUDIO_OUTPUT);
	fSidebar->AddItem(new SidebarItem("output", B_TRANSLATE("Output"),
		name.IsEmpty() ? none : name, GLYPH_SPEAKER,
		glyph_color(GLYPH_SPEAKER)));
	name = fModel.DefaultName(ROLE_AUDIO_INPUT);
	fSidebar->AddItem(new SidebarItem("input", B_TRANSLATE("Input"),
		name.IsEmpty() ? none : name, GLYPH_MICROPHONE,
		glyph_color(GLYPH_MICROPHONE)));
	fSidebar->AddItem(new SidebarItem("mixer", B_TRANSLATE("Mixer"),
		B_TRANSLATE("Volume of each application"), GLYPH_MIXER,
		glyph_color(GLYPH_MIXER)));
	fSidebar->AddItem(new SidebarItem("midi", B_TRANSLATE("MIDI"),
		soundfont_name(), GLYPH_MIDI, glyph_color(GLYPH_MIDI)));

	fSidebar->AddItem(new SidebarItem(B_TRANSLATE("Video")));
	name = fModel.DefaultName(ROLE_VIDEO_INPUT);
	fSidebar->AddItem(new SidebarItem("camera", B_TRANSLATE("Camera"),
		name.IsEmpty() ? none : name, GLYPH_CAMERA,
		glyph_color(GLYPH_CAMERA)));

	if (fModel.CountDevices() > 0)
		fSidebar->AddItem(new SidebarItem(B_TRANSLATE("Devices")));

	static const uint32 kRoles[] = { ROLE_AUDIO_OUTPUT, ROLE_AUDIO_INPUT,
		ROLE_VIDEO_INPUT, ROLE_VIDEO_OUTPUT };
	for (int32 i = 0; i < fModel.CountDevices(); i++) {
		const DeviceEntry* device = fModel.DeviceAt(i);

		BString uses;
		for (size_t j = 0; j < B_COUNT_OF(kRoles); j++) {
			const NodeEntry* node = device->NodeFor(kRoles[j]);
			if (node == NULL || !fModel.IsDefault(node->info, kRoles[j]))
				continue;
			if (!uses.IsEmpty())
				uses << ", ";
			uses << MediaModel::RoleName(kRoles[j]);
		}

		BString subtitle;
		if (uses.IsEmpty())
			subtitle = device->Description();
		else {
			subtitle = B_TRANSLATE("In use: %uses%");
			subtitle.ReplaceFirst("%uses%", uses);
		}

		fSidebar->AddItem(new SidebarItem(device->Key(), device->name,
			subtitle, device->Glyph(), device_glyph_color()));
	}

	fSidebar->SelectKey(fPageKey);
}


void
MediaWindow::_ShowPage(const char* key)
{
	if (fPage != NULL && fPageKey == key)
		return;

	Page* page = _CreatePage(key);
	if (page == NULL) {
		// A device that is not (yet) there, as right after the services
		// started: show it when it comes, unless another page is chosen.
		if (BString(key).StartsWith("device:"))
			fPendingKey = key;
		page = _CreatePage("output");
		if (page == NULL)
			return;
	} else
		fPendingKey = "";

	if (fPage != NULL) {
		fPage->RemoveSelf();
		delete fPage;
	}

	fPage = page;
	fPageKey = page->Key();
	fWantedKey = fPageKey;
	fPageHolder->GroupLayout()->AddView(fPage);
	fPage->Rebuild();
	fSidebar->SelectKey(fPageKey);
}


Page*
MediaWindow::_CreatePage(const char* key)
{
	BString name(key);
	if (name == "output")
		return new OutputPage(&fModel);
	if (name == "input")
		return new InputPage(&fModel);
	if (name == "mixer")
		return new MixerPage(&fModel);
	if (name == "midi")
		return new MidiPage(&fModel);
	if (name == "camera")
		return new CameraPage(&fModel);

	const DeviceEntry* device = fModel.DeviceFor(key);
	if (device != NULL)
		return new DevicePage(*device, &fModel);
	return NULL;
}


void
MediaWindow::_ShowServerDown()
{
	if (fPageKey == kServerPageKey || fRestartThread >= 0)
		return;

	if (fPage != NULL) {
		if (fPageKey != kRestartPageKey)
			fWantedKey = fPageKey;
		fPage->RemoveSelf();
		delete fPage;
	}

	for (int32 i = fSidebar->CountItems() - 1; i >= 0; i--)
		delete fSidebar->RemoveItem(i);

	fPage = new MessagePage(kServerPageKey, GLYPH_WARNING,
		B_TRANSLATE("Media services are not running"),
		B_TRANSLATE("Sound and video need the media services. Start them to "
			"use and set up your devices."),
		B_TRANSLATE("Start media services"), new BMessage(kMsgStartServices),
		&fModel);
	fPageKey = kServerPageKey;
	fPageHolder->GroupLayout()->AddView(fPage);
	fPage->Rebuild();
}


void
MediaWindow::_SetDefault(BMessage* message)
{
	int32 role;
	int32 addon;
	int32 flavor;
	if (message->FindInt32("role", &role) != B_OK
		|| message->FindInt32("addon", &addon) != B_OK
		|| message->FindInt32("flavor", &flavor) != B_OK) {
		return;
	}

	std::vector<const NodeEntry*> nodes;
	fModel.NodesFor(role, nodes);
	const dormant_node_info* info = NULL;
	for (size_t i = 0; i < nodes.size(); i++) {
		if (nodes[i]->info.addon == addon
			&& nodes[i]->info.flavor_id == flavor) {
			info = &nodes[i]->info;
			break;
		}
	}
	if (info == NULL || fModel.IsDefault(*info, role))
		return;

	BMediaRoster* roster = BMediaRoster::Roster();
	status_t status = B_ERROR;
	switch (role) {
		case ROLE_AUDIO_OUTPUT:
			status = roster->SetAudioOutput(*info);
			break;
		case ROLE_AUDIO_INPUT:
			status = roster->SetAudioInput(*info);
			break;
		case ROLE_VIDEO_INPUT:
			status = roster->SetVideoInput(*info);
			break;
		case ROLE_VIDEO_OUTPUT:
			status = roster->SetVideoOutput(*info);
			break;
	}

	if (status != B_OK) {
		BString text(B_TRANSLATE("%device% could not be chosen: %error%."));
		text.ReplaceFirst("%device%", info->name);
		text.ReplaceFirst("%error%", strerror(status));
		BAlert* alert = new BAlert(B_TRANSLATE("Media"), text,
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go(NULL);
	}

	_Rescan(false);
}


status_t
MediaWindow::_RestartThread(void* data)
{
	// a messenger: the window may be gone ("Quit anyway") before we are done
	BMessenger* window = (BMessenger*)data;

	if (BMediaRoster::IsRunning())
		shutdown_media_server(B_INFINITE_TIMEOUT, &_RestartProgress, window);
	status_t status = launch_media_server(B_INFINITE_TIMEOUT,
		&_RestartProgress, window);
	if (status != B_OK && status != B_ALREADY_RUNNING) {
		// launch_media_server() needs a media roster, which an application
		// started without the media services does not get; and
		// "launch_roster stop" disabled the job as well.
		BLaunchRoster launchRoster;
		launchRoster.SetEnabled(kMediaServerJob, true);
		status = launchRoster.Start(kMediaServerJob);
	}
	if (status == B_ALREADY_RUNNING)
		status = B_OK;

	// the add-on server registers its devices a moment later
	for (int i = 0; status == B_OK && i < 50 && !BMediaRoster::IsRunning();
			i++) {
		snooze(100000);
	}
	BMediaRoster* roster = BMediaRoster::Roster();
	for (int i = 0; status == B_OK && roster != NULL && i < 30; i++) {
		dormant_node_info info;
		int32 count = 1;
		if (roster->GetDormantNodes(&info, &count, NULL, NULL, NULL,
				B_PHYSICAL_OUTPUT) == B_OK && count > 0) {
			break;
		}
		snooze(100000);
	}

	BMessage done(kMsgRestartDone);
	done.AddInt32("status", status);
	window->SendMessage(&done);
	delete window;
	return B_OK;
}


bool
MediaWindow::_RestartProgress(int stage, const char* message, void* cookie)
{
	BMessenger* window = (BMessenger*)cookie;
	BMessage progress(kMsgRestartProgress);
	progress.AddInt32("stage", stage);
	progress.AddString("text", message);
	window->SendMessage(&progress, (BHandler*)NULL, 0);
	return true;
}
