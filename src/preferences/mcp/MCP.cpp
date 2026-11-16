/* Copyright 2026 air/OS contributors. Distributed under the MIT License. */
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Clipboard.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>

#include <MCPServer.h>

#include <string.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MCPWindow"

static const uint32 kToggle = 'mtog';
static const uint32 kRefresh = 'mref';
static const uint32 kCopy = 'mcop';


class MCPWindow : public BWindow {
public:
	MCPWindow()
		:
		BWindow(BRect(0, 0, 570, 410), B_TRANSLATE("MCP"), B_TITLED_WINDOW,
			B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
		fRunner(NULL)
	{
		fEnabled = new BCheckBox("enabled", B_TRANSLATE("Enable MCP"),
			new BMessage(kToggle));
		fStatus = new BStringView("status", B_TRANSLATE("Connecting to MCP server…"));
		BTextView* description = new BTextView("description");
		description->SetText(B_TRANSLATE("Allow AI agents to use this computer through "
			"the Model Context Protocol. Connected agents can run commands and access "
			"files. Share the connection details only with agents you trust, on a "
			"trusted network. HTTP connections are not encrypted."));
		description->MakeEditable(false);
		description->MakeSelectable(false);
		description->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		description->SetExplicitMinSize(BSize(450, 65));
		fInfo = new BTextView("connection info");
		fInfo->MakeEditable(false);
		fInfo->SetFontAndColor(be_fixed_font);
		fInfo->SetInsets(8, 8, 8, 8);
		BScrollView* scroll = new BScrollView("connection scroll", fInfo, 0,
			false, true, B_FANCY_BORDER);
		scroll->SetExplicitMinSize(BSize(450, 225));
		fCopy = new BButton("copy", B_TRANSLATE("Copy connection info"),
			new BMessage(kCopy));
		fCopy->SetEnabled(false);
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fEnabled)
			.Add(description)
			.Add(fStatus)
			.Add(scroll)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(fCopy)
			.End();
		CenterOnScreen();
		BMessage refresh(kRefresh);
		fRunner = new BMessageRunner(BMessenger(this), &refresh, 3000000);
		PostMessage(kRefresh);
	}

	virtual ~MCPWindow()
	{
		delete fRunner;
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kCopy) {
			if (fCopy->IsEnabled() && be_clipboard->Lock()) {
				be_clipboard->Clear();
				be_clipboard->Data()->AddData("text/plain", B_MIME_TYPE,
					fInfo->Text(), fInfo->TextLength());
				be_clipboard->Commit();
				be_clipboard->Unlock();
			}
		} else if (message->what == kRefresh || message->what == kToggle)
			_Update(message->what == kToggle);
		else
			BWindow::MessageReceived(message);
	}

private:
	void _Update(bool toggle)
	{
		BMessenger server(kMCPServerSignature);
		if (!server.IsValid()) {
			be_roster->Launch(kMCPServerSignature);
			server = BMessenger(kMCPServerSignature);
		}
		BMessage request(toggle ? kMCPSetEnabled : kMCPGetStatus);
		if (toggle)
			request.AddBool("enabled", fEnabled->Value() == B_CONTROL_ON);
		BMessage reply;
		status_t error = server.SendMessage(&request, &reply, 1000000, 1000000);
		bool available = error == B_OK;
		if (available)
			reply.FindInt32("error", &error);
		bool enabled = false, running = false;
		reply.FindBool("enabled", &enabled);
		reply.FindBool("running", &running);
		fEnabled->SetValue(enabled ? B_CONTROL_ON : B_CONTROL_OFF);
		fEnabled->SetEnabled(available);
		BString status;
		if (error != B_OK) {
			status.SetToFormat(B_TRANSLATE("MCP error: %s"), strerror(error));
		} else {
			status = running ? B_TRANSLATE("MCP is on. This setting is saved for future boots.")
				: B_TRANSLATE("MCP is off. No network connections are accepted.");
		}
		fStatus->SetText(status);
		const char* info = "";
		if (running)
			reply.FindString("connection_info", &info);
		if (strcmp(fInfo->Text(), info) != 0)
			fInfo->SetText(info);
		fCopy->SetEnabled(running && info[0] != '\0');
	}

	BCheckBox* fEnabled;
	BStringView* fStatus;
	BTextView* fInfo;
	BButton* fCopy;
	BMessageRunner* fRunner;
};


class MCPApplication : public BApplication {
public:
	MCPApplication() : BApplication("application/x-vnd.vos-MCP"), fWindow(NULL) {}
	virtual void ReadyToRun()
	{
		fWindow = new MCPWindow;
		fWindow->Show();
	}
	virtual void RefsReceived(BMessage*) { _Activate(); }
	virtual void ArgvReceived(int32, char**) { _Activate(); }

private:
	void _Activate()
	{
		if (fWindow != NULL && fWindow->Lock()) {
			fWindow->Activate();
			fWindow->Unlock();
		}
	}
	MCPWindow* fWindow;
};


int
main()
{
	MCPApplication app;
	app.Run();
	return 0;
}
