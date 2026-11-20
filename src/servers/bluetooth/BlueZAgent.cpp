/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "BlueZAgent.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <list>

#include <systemd/sd-bus.h>

#include <Alert.h>
#include <Autolock.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <Locker.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>


static const char* kAgentPath = "/org/vos/bluetooth/agent";
static const char* kAgentInterface = "org.bluez.Agent1";
static const char* kBlueZService = "org.bluez";
static const char* kRejected = "org.bluez.Error.Rejected";
static const char* kCanceled = "org.bluez.Error.Canceled";


enum request_kind {
	kRequestPinCode,
	kRequestPasskey,
	kRequestConfirmation,
	kRequestAuthorization,
	kAuthorizeService
};


/*!	A request of BlueZ the user answers; the answer is sent from the bus
	thread (sd-bus is not to be used from two threads). */
struct agent_request {
	sd_bus_message*	message;
	request_kind	kind;
	BString			device;
	uint32			passkey;
	BString			uuid;

	bool			accepted;
	BString			pinCode;
	uint32			enteredPasskey;
};


static BLocker sLock("bluez agent");
static std::list<agent_request*> sAnswered;
static int sWakeFD = -1;


// #pragma mark - code entry window


static const uint32 kMsgEntered = 'Entr';


class CodeWindow : public BWindow {
public:
	CodeWindow(const char* text, sem_id done, BString* code, bool* ok)
		:
		BWindow(BRect(0, 0, 300, 100), "Bluetooth", B_MODAL_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
				| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
		fDone(done),
		fCode(code),
		fOK(ok)
	{
		fControl = new BTextControl("code", NULL, "", NULL);
		BButton* cancel = new BButton("Cancel",
			new BMessage(B_QUIT_REQUESTED));
		BButton* pair = new BButton("Pair", new BMessage(kMsgEntered));
		pair->MakeDefault(true);

		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(new BStringView("text", text))
			.Add(fControl)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancel)
				.Add(pair)
			.End();
		fControl->MakeFocus(true);
		CenterOnScreen();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgEntered) {
			*fCode = fControl->Text();
			*fOK = true;
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		BWindow::MessageReceived(message);
	}

	virtual bool QuitRequested()
	{
		release_sem(fDone);
		return true;
	}

private:
	BTextControl*	fControl;
	sem_id			fDone;
	BString*		fCode;
	bool*			fOK;
};


static bool
ask_for_code(const char* text, BString& code)
{
	sem_id done = create_sem(0, "bluetooth code");
	if (done < 0)
		return false;
	bool ok = false;
	CodeWindow* window = new CodeWindow(text, done, &code, &ok);
	window->Show();
	while (acquire_sem(done) == B_INTERRUPTED)
		;
	delete_sem(done);
	return ok && code.Length() > 0;
}


static bool
ask_to_accept(const char* text, const char* accept)
{
	BAlert* alert = new BAlert("Bluetooth", text, "Cancel", accept, NULL,
		B_WIDTH_AS_USUAL, B_IDEA_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	return alert->Go() == 1;
}


static status_t
answer_thread(void* data)
{
	agent_request* request = (agent_request*)data;
	BString text;
	const char* device = request->device.String();

	switch (request->kind) {
		case kRequestPinCode:
			text.SetToFormat("Enter the PIN code for \"%s\":", device);
			request->accepted = ask_for_code(text, request->pinCode);
			break;
		case kRequestPasskey:
		{
			BString code;
			text.SetToFormat("Enter the passkey shown on \"%s\":", device);
			request->accepted = ask_for_code(text, code);
			request->enteredPasskey = strtoul(code.String(), NULL, 10);
			break;
		}
		case kRequestConfirmation:
			text.SetToFormat("Does \"%s\" show the code %06" B_PRIu32 "?",
				device, request->passkey);
			request->accepted = ask_to_accept(text, "Pair");
			break;
		case kRequestAuthorization:
			text.SetToFormat("Allow \"%s\" to pair with this computer?",
				device);
			request->accepted = ask_to_accept(text, "Allow");
			break;
		case kAuthorizeService:
			text.SetToFormat("Allow \"%s\" to use the service %s?", device,
				request->uuid.String());
			request->accepted = ask_to_accept(text, "Allow");
			break;
	}

	BAutolock _(sLock);
	sAnswered.push_back(request);
	uint64_t one = 1;
	if (sWakeFD >= 0 && write(sWakeFD, &one, sizeof(one)) < 0)
		fprintf(stderr, "bluetooth_server: agent wake-up: %s\n",
			strerror(errno));
	return B_OK;
}


// #pragma mark - Agent1 methods


static BString
device_name(sd_bus* bus, const char* path)
{
	char* alias = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	BString name(path);
	if (sd_bus_get_property_string(bus, kBlueZService, path,
			"org.bluez.Device1", "Alias", &error, &alias) >= 0) {
		name = alias;
		free(alias);
	}
	sd_bus_error_free(&error);
	return name;
}


static int
ask_user(sd_bus_message* message, request_kind kind, const char* path,
	uint32 passkey, const char* uuid)
{
	agent_request* request = new(std::nothrow) agent_request;
	if (request == NULL)
		return -ENOMEM;
	request->message = sd_bus_message_ref(message);
	request->kind = kind;
	request->device = device_name(sd_bus_message_get_bus(message), path);
	request->passkey = passkey;
	request->uuid = uuid != NULL ? uuid : "";
	request->accepted = false;
	request->enteredPasskey = 0;

	thread_id thread = spawn_thread(answer_thread, "bluetooth agent request",
		B_NORMAL_PRIORITY, request);
	if (thread < 0 || resume_thread(thread) != B_OK) {
		sd_bus_message_unref(request->message);
		delete request;
		return sd_bus_reply_method_errorf(message, kRejected,
			"cannot ask the user");
	}
	// answered later, see BlueZAgent::_Thread()
	return 1;
}


static int
method_release(sd_bus_message* message, void* data, sd_bus_error* error)
{
	return sd_bus_reply_method_return(message, "");
}


static int
method_request_pin_code(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	const char* path;
	int status = sd_bus_message_read(message, "o", &path);
	if (status < 0)
		return status;
	return ask_user(message, kRequestPinCode, path, 0, NULL);
}


static int
method_request_passkey(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	const char* path;
	int status = sd_bus_message_read(message, "o", &path);
	if (status < 0)
		return status;
	return ask_user(message, kRequestPasskey, path, 0, NULL);
}


static void
show_code(sd_bus_message* message, const char* path, const char* code)
{
	BString text;
	text.SetToFormat("Enter %s on \"%s\" to pair it with this computer.",
		code, device_name(sd_bus_message_get_bus(message), path).String());
	BAlert* alert = new BAlert("Bluetooth", text, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->Go(NULL);
}


static int
method_display_pin_code(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	const char* path;
	const char* pinCode;
	int status = sd_bus_message_read(message, "os", &path, &pinCode);
	if (status < 0)
		return status;
	show_code(message, path, pinCode);
	return sd_bus_reply_method_return(message, "");
}


static int
method_display_passkey(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	const char* path;
	uint32_t passkey;
	uint16_t entered;
	int status = sd_bus_message_read(message, "ouq", &path, &passkey,
		&entered);
	if (status < 0)
		return status;
	// (called again for each digit typed on the device: shown once)
	if (entered == 0) {
		char code[16];
		snprintf(code, sizeof(code), "%06u", (unsigned)passkey);
		show_code(message, path, code);
	}
	return sd_bus_reply_method_return(message, "");
}


static int
method_request_confirmation(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	const char* path;
	uint32_t passkey;
	int status = sd_bus_message_read(message, "ou", &path, &passkey);
	if (status < 0)
		return status;
	return ask_user(message, kRequestConfirmation, path, passkey, NULL);
}


static int
method_request_authorization(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	const char* path;
	int status = sd_bus_message_read(message, "o", &path);
	if (status < 0)
		return status;
	return ask_user(message, kRequestAuthorization, path, 0, NULL);
}


static int
method_authorize_service(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	const char* path;
	const char* uuid;
	int status = sd_bus_message_read(message, "os", &path, &uuid);
	if (status < 0)
		return status;
	return ask_user(message, kAuthorizeService, path, 0, uuid);
}


static int
method_cancel(sd_bus_message* message, void* data, sd_bus_error* error)
{
	// TODO(linux backend): close the window of the request BlueZ gave up
	// on; its answer is still sent, and BlueZ ignores it.
	return sd_bus_reply_method_return(message, "");
}


static const sd_bus_vtable kAgentVTable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Release", "", "", method_release, 0),
	SD_BUS_METHOD("RequestPinCode", "o", "s", method_request_pin_code,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("DisplayPinCode", "os", "", method_display_pin_code,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("RequestPasskey", "o", "u", method_request_passkey,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("DisplayPasskey", "ouq", "", method_display_passkey,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("RequestConfirmation", "ou", "",
		method_request_confirmation, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("RequestAuthorization", "o", "",
		method_request_authorization, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("AuthorizeService", "os", "", method_authorize_service,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("Cancel", "", "", method_cancel,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END
};


static void
send_answer(agent_request* request)
{
	sd_bus_message* message = request->message;
	if (!request->accepted) {
		sd_bus_reply_method_errorf(message,
			request->kind == kRequestPinCode || request->kind == kRequestPasskey
				? kCanceled : kRejected, "the user said no");
	} else if (request->kind == kRequestPinCode) {
		sd_bus_reply_method_return(message, "s", request->pinCode.String());
	} else if (request->kind == kRequestPasskey) {
		sd_bus_reply_method_return(message, "u",
			(uint32_t)request->enteredPasskey);
	} else
		sd_bus_reply_method_return(message, "");

	sd_bus_message_unref(message);
	delete request;
}


static void
register_agent(sd_bus* bus)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int status = sd_bus_call_method(bus, kBlueZService, "/org/bluez",
		"org.bluez.AgentManager1", "RegisterAgent", &error, NULL, "os",
		kAgentPath, "KeyboardDisplay");
	if (status >= 0) {
		status = sd_bus_call_method(bus, kBlueZService, "/org/bluez",
			"org.bluez.AgentManager1", "RequestDefaultAgent", &error, NULL,
			"o", kAgentPath);
	}
	if (status < 0) {
		fprintf(stderr, "bluetooth_server: cannot register the pairing "
			"agent with BlueZ: %s\n", error.message != NULL ? error.message
				: strerror(-status));
	} else
		printf("bluetooth_server: pairing agent registered with BlueZ\n");
	sd_bus_error_free(&error);
}


static int
bluez_owner_changed(sd_bus_message* message, void* data, sd_bus_error* error)
{
	const char* name;
	const char* oldOwner;
	const char* newOwner;
	if (sd_bus_message_read(message, "sss", &name, &oldOwner, &newOwner) >= 0
		&& strcmp(name, kBlueZService) == 0 && newOwner[0] != '\0')
		*(bool*)data = true;
	return 0;
}


// #pragma mark - BlueZAgent


BlueZAgent::BlueZAgent()
	:
	fThread(-1),
	fWakeFD(-1),
	fQuit(false)
{
}


BlueZAgent::~BlueZAgent()
{
	Stop();
}


status_t
BlueZAgent::Start()
{
	fWakeFD = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (fWakeFD < 0)
		return B_ERROR;
	sWakeFD = fWakeFD;

	fThread = spawn_thread(_ThreadEntry, "bluez agent", B_NORMAL_PRIORITY,
		this);
	if (fThread < 0)
		return fThread;
	return resume_thread(fThread);
}


void
BlueZAgent::Stop()
{
	if (fThread < 0)
		return;
	fQuit = true;
	uint64_t one = 1;
	if (write(fWakeFD, &one, sizeof(one)) < 0)
		fprintf(stderr, "bluetooth_server: agent stop: %s\n", strerror(errno));
	status_t status;
	wait_for_thread(fThread, &status);
	fThread = -1;

	BAutolock _(sLock);
	sWakeFD = -1;
	close(fWakeFD);
	fWakeFD = -1;
}


/*static*/ status_t
BlueZAgent::_ThreadEntry(void* data)
{
	((BlueZAgent*)data)->_Thread();
	return B_OK;
}


void
BlueZAgent::_Thread()
{
	sd_bus* bus = NULL;
	int status = sd_bus_open_system(&bus);
	if (status < 0) {
		fprintf(stderr, "bluetooth_server: no system bus: %s\n",
			strerror(-status));
		return;
	}

	bool bluezAppeared = false;
	status = sd_bus_add_object_vtable(bus, NULL, kAgentPath, kAgentInterface,
		kAgentVTable, this);
	if (status >= 0) {
		// BlueZ forgets its agents when it restarts
		status = sd_bus_match_signal(bus, NULL, "org.freedesktop.DBus",
			"/org/freedesktop/DBus", "org.freedesktop.DBus",
			"NameOwnerChanged", bluez_owner_changed, &bluezAppeared);
	}
	if (status < 0) {
		fprintf(stderr, "bluetooth_server: cannot export the pairing agent: "
			"%s\n", strerror(-status));
		sd_bus_unref(bus);
		return;
	}

	register_agent(bus);

	while (!fQuit) {
		while ((status = sd_bus_process(bus, NULL)) > 0)
			;
		if (status < 0)
			break;

		if (bluezAppeared) {
			bluezAppeared = false;
			register_agent(bus);
		}

		{
			BAutolock _(sLock);
			while (!sAnswered.empty()) {
				send_answer(sAnswered.front());
				sAnswered.pop_front();
			}
		}
		sd_bus_flush(bus);

		struct pollfd fds[2] = {
			{ sd_bus_get_fd(bus), (short)sd_bus_get_events(bus), 0 },
			{ fWakeFD, POLLIN, 0 }
		};
		if (poll(fds, 2, -1) < 0 && errno != EINTR)
			break;
		if ((fds[1].revents & POLLIN) != 0) {
			uint64_t count;
			if (read(fWakeFD, &count, sizeof(count)) < 0)
				fprintf(stderr, "bluetooth_server: agent: %s\n",
					strerror(errno));
		}
	}

	sd_bus_call_method(bus, kBlueZService, "/org/bluez",
		"org.bluez.AgentManager1", "UnregisterAgent", NULL, NULL, "o",
		kAgentPath);
	sd_bus_flush_close_unref(bus);
}
