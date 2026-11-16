/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


/*!	Checks the accessibility interface (haiku#54) over its protocol:
	subscribes to the accessibility_server like an assistive technology,
	queries the accessibility tree of AccessibilityTestApp, performs actions
	and waits for the events they cause.

	  accessibility_tree_test             run the checks
	  accessibility_tree_test --dump SIG  print the tree of a running app
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <Autolock.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <OS.h>
#include <Roster.h>
#include <String.h>

#include <Accessible.h>
#include <AccessibilityProtocol.h>


static const char* kTestAppSignature
	= "application/x-vnd.vos-AccessibilityTestApp";
static const bigtime_t kTimeout = 2000000;

static int32 sPassed = 0;
static int32 sFailed = 0;


#define CHECK(condition, ...) \
	do { \
		if (condition) { \
			sPassed++; \
			printf("PASS: "); \
		} else { \
			sFailed++; \
			printf("FAIL: "); \
		} \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} while (false)


class EventCollector : public BLooper {
public:
	EventCollector()
		:
		BLooper("events")
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == B_ACCESSIBILITY_EVENT) {
			BAutolock locker(fLock);
			fEvents.AddItem(new BMessage(*message));
			return;
		}
		BLooper::MessageReceived(message);
	}

	void Clear()
	{
		BAutolock locker(fLock);
		fEvents.MakeEmpty();
	}

	//! Waits for an event of \a team; name may be NULL.
	bool WaitFor(int32 event, team_id team, const char* name,
		BMessage* _event)
	{
		bigtime_t end = system_time() + kTimeout;
		while (system_time() < end) {
			{
				BAutolock locker(fLock);
				for (int32 i = 0; i < fEvents.CountItems(); i++) {
					BMessage* message = fEvents.ItemAt(i);
					if (message->GetInt32("event", 0) != event
						|| message->GetInt32("team", -1) != team) {
						continue;
					}
					BMessage node;
					message->FindMessage("node", &node);
					if (name != NULL && strcmp(node.GetString("name", ""),
							name) != 0) {
						continue;
					}
					if (_event != NULL)
						*_event = *message;
					return true;
				}
			}
			snooze(20000);
		}
		return false;
	}

private:
	BLocker				fLock;
	BObjectList<BMessage, true> fEvents;
};


static void
dump_node(const BMessage& node, int32 level)
{
	BString states;
	uint64 bits = (uint64)node.GetInt64("states", 0);
	static const char* kStateNames[] = { "focusable", "focused", "disabled",
		"checkable", "checked", "mixed", "pressed", "selectable", "selected",
		"multi-selectable", "expandable", "expanded", "editable", "read-only",
		"multi-line", "protected", "invisible", "offscreen", "active", "modal",
		"default", "required", "invalid", "busy", "has-popup", "visited",
		"horizontal", "vertical" };
	for (uint32 i = 0; i < sizeof(kStateNames) / sizeof(kStateNames[0]); i++) {
		if ((bits & (1ULL << i)) != 0) {
			if (!states.IsEmpty())
				states << ",";
			states << kStateNames[i];
		}
	}

	printf("%*s[%s] \"%s\"", (int)level * 2, "", node.GetString("role_name", "?"),
		node.GetString("name", ""));
	const char* value;
	if (node.FindString("value", &value) == B_OK && value[0] != '\0')
		printf(" value=\"%s\"", value);
	if (!states.IsEmpty())
		printf(" {%s}", states.String());
	printf(" id=%#" B_PRIx64 "\n", node.GetInt64("id", 0));

	BMessage child;
	for (int32 i = 0; node.FindMessage("child", i, &child) == B_OK; i++)
		dump_node(child, level + 1);
}


static bool
find_node(const BMessage& node, const char* name, int32 role, BMessage& found)
{
	if (strcmp(node.GetString("name", ""), name) == 0
		&& (role < 0 || node.GetInt32("role", -1) == role)) {
		found = node;
		return true;
	}
	BMessage child;
	for (int32 i = 0; node.FindMessage("child", i, &child) == B_OK; i++) {
		if (find_node(child, name, role, found))
			return true;
	}
	return false;
}


//! Checks that child IDs and parent IDs agree in the whole tree.
static bool
check_structure(const BMessage& node, int32& count)
{
	count++;
	int64 id = node.GetInt64("id", 0);
	if (id == 0) {
		printf("  node \"%s\" has no ID\n", node.GetString("name", ""));
		return false;
	}
	BMessage child;
	for (int32 i = 0; node.FindMessage("child", i, &child) == B_OK; i++) {
		if (child.GetInt64("id", 0) != node.GetInt64("child_ids", i, 0)) {
			printf("  child %" B_PRId32 " of \"%s\": ID mismatch\n", i,
				node.GetString("name", ""));
			return false;
		}
		if (child.GetInt64("parent", 0) != id) {
			printf("  \"%s\" names parent %#" B_PRIx64 ", not %#" B_PRIx64
				"\n", child.GetString("name", ""), child.GetInt64("parent", 0),
				id);
			return false;
		}
		if (!check_structure(child, count))
			return false;
	}
	type_code type;
	int32 idCount = 0;
	node.GetInfo("child_ids", &type, &idCount);
	if (node.GetInt32("child_count", -1) != idCount) {
		printf("  \"%s\": child_count does not match child_ids\n",
			node.GetString("name", ""));
		return false;
	}
	return true;
}


static bool
bridge_thread_exists(team_id team)
{
	thread_info info;
	int32 cookie = 0;
	while (get_next_thread_info(team, &cookie, &info) == B_OK) {
		if (strcmp(info.name, "accessibility bridge") == 0)
			return true;
	}
	return false;
}


static status_t
query(const BMessenger& bridge, BMessage& request, BMessage& reply)
{
	status_t status = bridge.SendMessage(&request, &reply, kTimeout, kTimeout);
	if (status != B_OK)
		return status;
	return reply.GetInt32("status", B_ERROR);
}


static status_t
get_node(const BMessenger& bridge, int64 id, int32 depth, BMessage& node)
{
	BMessage request(B_ACCESSIBILITY_GET_NODE);
	request.AddInt64("id", id);
	request.AddInt32("depth", depth);
	BMessage reply;
	status_t status = query(bridge, request, reply);
	if (status == B_OK)
		status = reply.FindMessage("node", &node);
	return status;
}


static status_t
perform(const BMessenger& bridge, int64 id, accessible_action action)
{
	BMessage request(B_ACCESSIBILITY_PERFORM_ACTION);
	request.AddInt64("id", id);
	request.AddInt32("action", action);
	BMessage reply;
	return query(bridge, request, reply);
}


static BMessenger
subscribe(EventCollector* collector, BMessenger& server)
{
	server = BMessenger(B_ACCESSIBILITY_SERVER_SIGNATURE);
	BMessage request(B_ACCESSIBILITY_SUBSCRIBE);
	request.AddMessenger("client", BMessenger(collector));
	BMessage reply;
	if (!server.IsValid()
		|| server.SendMessage(&request, &reply, kTimeout, kTimeout) != B_OK) {
		return BMessenger();
	}
	return server;
}


static BMessenger
wait_for_bridge(const BMessenger& server, team_id team)
{
	bigtime_t end = system_time() + 5000000;
	while (system_time() < end) {
		BMessage request(B_ACCESSIBILITY_GET_APPS);
		BMessage reply;
		if (server.SendMessage(&request, &reply, kTimeout, kTimeout) == B_OK) {
			BMessage app;
			for (int32 i = 0; reply.FindMessage("app", i, &app) == B_OK; i++) {
				BMessenger bridge;
				if (app.GetInt32("team", -1) == team
					&& app.FindMessenger("bridge", &bridge) == B_OK) {
					return bridge;
				}
			}
		}
		snooze(100000);
	}
	return BMessenger();
}


static int
dump(const char* signature)
{
	app_info info;
	if (be_roster->GetAppInfo(signature, &info) != B_OK) {
		fprintf(stderr, "%s is not running\n", signature);
		return 1;
	}

	EventCollector* collector = new EventCollector();
	collector->Run();
	BMessenger server;
	if (!subscribe(collector, server).IsValid()) {
		fprintf(stderr, "cannot reach the accessibility_server\n");
		return 1;
	}
	BMessenger bridge = wait_for_bridge(server, info.team);
	if (!bridge.IsValid()) {
		fprintf(stderr, "%s did not register\n", signature);
		return 1;
	}

	BMessage request(B_ACCESSIBILITY_GET_ROOT);
	request.AddInt32("depth", -1);
	BMessage reply, root;
	if (query(bridge, request, reply) != B_OK
		|| reply.FindMessage("node", &root) != B_OK) {
		fprintf(stderr, "no tree\n");
		return 1;
	}
	dump_node(root, 0);

	BMessage unsubscribe(B_ACCESSIBILITY_UNSUBSCRIBE);
	unsubscribe.AddMessenger("client", BMessenger(collector));
	server.SendMessage(&unsubscribe);
	return 0;
}


int
main(int argc, char** argv)
{
	BApplication app("application/x-vnd.vos-accessibility_tree_test");

	if (argc == 3 && strcmp(argv[1], "--dump") == 0)
		return dump(argv[2]);

	// start the test application
	team_id team = -1;
	app_info info;
	if (be_roster->GetAppInfo(kTestAppSignature, &info) == B_OK)
		team = info.team;
	else {
		status_t status = be_roster->Launch(kTestAppSignature, (BMessage*)NULL,
			&team);
		if (status != B_OK) {
			// not registered in the MIME database: look next to us
			BString path(argv[0]);
			int32 slash = path.FindLast('/');
			path.Truncate(slash >= 0 ? slash + 1 : 0);
			path << "AccessibilityTestApp";
			entry_ref ref;
			if (get_ref_for_path(path.String(), &ref) == B_OK)
				status = be_roster->Launch(&ref, (BMessage*)NULL, &team);
		}
		if (status != B_OK) {
			fprintf(stderr, "cannot launch AccessibilityTestApp: %s\n",
				strerror(status));
			return 1;
		}
		snooze(1500000);
	}
	printf("AccessibilityTestApp is team %" B_PRId32 "\n", team);

	bool otherClients = be_roster->IsRunning(B_SCREEN_READER_SIGNATURE);
	if (!otherClients) {
		CHECK(!bridge_thread_exists(team),
			"no accessibility bridge while no assistive technology runs");
	}

	EventCollector* collector = new EventCollector();
	collector->Run();
	BMessenger server;
	bigtime_t start = system_time();
	CHECK(subscribe(collector, server).IsValid(),
		"subscribed to the accessibility_server");
	BMessenger bridge = wait_for_bridge(server, team);
	CHECK(bridge.IsValid(), "test app registered its bridge after %" B_PRId64
		" ms", (system_time() - start) / 1000);
	if (!bridge.IsValid())
		return 1;
	CHECK(bridge_thread_exists(team), "bridge thread runs while subscribed");

	// the whole tree
	BMessage request(B_ACCESSIBILITY_GET_ROOT);
	request.AddInt32("depth", -1);
	BMessage reply, root;
	bigtime_t queryStart = system_time();
	CHECK(query(bridge, request, reply) == B_OK
		&& reply.FindMessage("node", &root) == B_OK,
		"GET_ROOT answered in %" B_PRId64 " us", system_time() - queryStart);
	dump_node(root, 0);

	CHECK(root.GetInt32("role", -1) == B_ACCESSIBLE_ROLE_APPLICATION,
		"root is an application");
	int32 count = 0;
	CHECK(check_structure(root, count), "IDs, parents and children agree "
		"(%" B_PRId32 " nodes)", count);

	BMessage window, node;
	CHECK(find_node(root, "Accessibility Test", B_ACCESSIBLE_ROLE_WINDOW,
		window), "window \"Accessibility Test\"");
	CHECK(find_node(root, "Press me", B_ACCESSIBLE_ROLE_BUTTON, node)
		&& (node.GetInt64("states", 0) & B_ACCESSIBLE_STATE_FOCUSED) != 0,
		"button \"Press me\" has the focus");

	BMessage checkBox;
	CHECK(find_node(root, "Enable feature", B_ACCESSIBLE_ROLE_CHECK_BOX,
		checkBox) && (checkBox.GetInt64("states", 0)
			& B_ACCESSIBLE_STATE_CHECKED) == 0,
		"check box \"Enable feature\", not checked");

	BMessage textField;
	CHECK(find_node(root, "Name", B_ACCESSIBLE_ROLE_TEXT_FIELD, textField)
		&& strcmp(textField.GetString("value", ""), "Ada") == 0
		&& textField.GetInt32("child_count", -1) == 0,
		"text field \"Name\" = \"Ada\", its text view hidden");

	BMessage slider;
	CHECK(find_node(root, "Volume", B_ACCESSIBLE_ROLE_SLIDER, slider)
		&& slider.GetDouble("current", -1) == 40
		&& slider.GetDouble("minimum", -1) == 0
		&& slider.GetDouble("maximum", -1) == 100,
		"slider \"Volume\" 40 of 0-100");

	BMessage list;
	CHECK(find_node(root, "Fruits", B_ACCESSIBLE_ROLE_LIST, list)
		&& list.GetInt32("child_count", 0) == 3, "list \"Fruits\", 3 items");
	BMessage banana;
	CHECK(find_node(list, "Banana", B_ACCESSIBLE_ROLE_LIST_ITEM, banana)
		&& banana.GetInt32("index", -1) == 1
		&& banana.GetInt32("count", -1) == 3, "list item \"Banana\", 2 of 3");

	CHECK(find_node(root, "Settings", B_ACCESSIBLE_ROLE_BUTTON, node)
		&& strcmp(node.GetString("description", ""), "Opens the settings")
			== 0, "unlabeled button named \"Settings\" by the app");

	BMessage document;
	CHECK(find_node(root, "Sample document", B_ACCESSIBLE_ROLE_DOCUMENT,
		document) && document.GetInt32("child_count", 0) == 2,
		"custom tree: document with 2 children");
	CHECK(find_node(document, "Welcome", B_ACCESSIBLE_ROLE_HEADING, node)
		&& node.GetInt32("level", 0) == 1, "custom tree: heading level 1");
	CHECK(find_node(document, "Haiku website", B_ACCESSIBLE_ROLE_LINK, node)
		&& strcmp(node.GetString("url", ""), "https://www.haiku-os.org/")
			== 0, "custom tree: link with URL");

	// single nodes
	BMessage single;
	CHECK(get_node(bridge, checkBox.GetInt64("id", 0), 0, single) == B_OK
		&& strcmp(single.GetString("name", ""), "Enable feature") == 0,
		"GET_NODE of the check box");
	CHECK(get_node(bridge, 0x7fffffff00000000LL, 0, single) != B_OK,
		"GET_NODE of an unknown ID fails");

	BMessage textRequest(B_ACCESSIBILITY_GET_TEXT);
	textRequest.AddInt64("id", textField.GetInt64("id", 0));
	textRequest.AddInt32("start", 0);
	textRequest.AddInt32("end", 100);
	CHECK(query(bridge, textRequest, reply) == B_OK
		&& strcmp(reply.GetString("text", ""), "Ada") == 0,
		"GET_TEXT of the text field");

	// actions and the events they cause
	BMessage event;
	collector->Clear();
	CHECK(perform(bridge, textField.GetInt64("id", 0),
		B_ACCESSIBLE_ACTION_FOCUS) == B_OK, "FOCUS the text field");
	CHECK(collector->WaitFor(B_ACCESSIBLE_EVENT_FOCUS_CHANGED, team, "Name",
		&event), "focus event for \"Name\"");
	BMessage eventNode;
	event.FindMessage("node", &eventNode);
	CHECK(eventNode.GetInt32("role", -1) == B_ACCESSIBLE_ROLE_TEXT_FIELD
		&& (eventNode.GetInt64("states", 0) & B_ACCESSIBLE_STATE_FOCUSED)
			!= 0
		&& strcmp(eventNode.GetString("value", ""), "Ada") == 0,
		"focus event node: text field, focused, \"Ada\"");
	BMessage eventWindow;
	CHECK(event.FindMessage("window", &eventWindow) == B_OK
		&& strcmp(eventWindow.GetString("name", ""), "Accessibility Test")
			== 0, "focus event carries the window");

	// value events are only sent for the focused control
	CHECK(perform(bridge, checkBox.GetInt64("id", 0),
		B_ACCESSIBLE_ACTION_FOCUS) == B_OK
		&& collector->WaitFor(B_ACCESSIBLE_EVENT_FOCUS_CHANGED, team,
			"Enable feature", NULL), "FOCUS the check box");
	collector->Clear();
	CHECK(perform(bridge, checkBox.GetInt64("id", 0),
		B_ACCESSIBLE_ACTION_PRESS) == B_OK, "PRESS the check box");
	CHECK(collector->WaitFor(B_ACCESSIBLE_EVENT_VALUE_CHANGED, team,
		"Enable feature", &event), "value event for the check box");
	event.FindMessage("node", &eventNode);
	CHECK((eventNode.GetInt64("states", 0) & B_ACCESSIBLE_STATE_CHECKED) != 0,
		"check box is checked in the event");
	CHECK(get_node(bridge, checkBox.GetInt64("id", 0), 0, single) == B_OK
		&& (single.GetInt64("states", 0) & B_ACCESSIBLE_STATE_CHECKED) != 0,
		"check box is checked in the tree");

	CHECK(perform(bridge, list.GetInt64("id", 0),
		B_ACCESSIBLE_ACTION_FOCUS) == B_OK
		&& collector->WaitFor(B_ACCESSIBLE_EVENT_FOCUS_CHANGED, team,
			"Fruits", NULL), "FOCUS the list");
	collector->Clear();
	CHECK(perform(bridge, banana.GetInt64("id", 0),
		B_ACCESSIBLE_ACTION_SELECT) == B_OK, "SELECT \"Banana\"");
	CHECK(collector->WaitFor(B_ACCESSIBLE_EVENT_SELECTION_CHANGED, team,
		"Banana", &event), "selection event for \"Banana\"");
	BMessage container;
	CHECK(event.FindMessage("container", &container) == B_OK
		&& strcmp(container.GetString("value", ""), "Banana") == 0,
		"selection event names the list, value \"Banana\"");

	CHECK(perform(bridge, slider.GetInt64("id", 0),
		B_ACCESSIBLE_ACTION_FOCUS) == B_OK
		&& collector->WaitFor(B_ACCESSIBLE_EVENT_FOCUS_CHANGED, team,
			"Volume", NULL), "FOCUS the slider");
	collector->Clear();
	CHECK(perform(bridge, slider.GetInt64("id", 0),
		B_ACCESSIBLE_ACTION_INCREMENT) == B_OK, "INCREMENT the slider");
	CHECK(collector->WaitFor(B_ACCESSIBLE_EVENT_VALUE_CHANGED, team, "Volume",
		&event), "value event for the slider");
	event.FindMessage("node", &eventNode);
	CHECK(eventNode.GetDouble("current", -1) == 45,
		"slider is 45 in the event (%g)", eventNode.GetDouble("current", -1));

	collector->Clear();
	CHECK(perform(bridge, window.GetInt64("id", 0),
		B_ACCESSIBLE_ACTION_PRESS) == B_NOT_SUPPORTED,
		"unsupported action is refused");

	BMessage focusRequest(B_ACCESSIBILITY_GET_FOCUS);
	status_t focusStatus = query(bridge, focusRequest, reply);
	if (focusStatus == B_OK) {
		BMessage focus;
		CHECK(reply.FindMessage("node", &focus) == B_OK
			&& strcmp(focus.GetString("name", ""), "Volume") == 0,
			"GET_FOCUS: \"Volume\"");
	} else
		printf("SKIP: GET_FOCUS (the test window is not active)\n");

	// unsubscribing turns the bridge off again
	BMessage unsubscribe(B_ACCESSIBILITY_UNSUBSCRIBE);
	unsubscribe.AddMessenger("client", BMessenger(collector));
	server.SendMessage(&unsubscribe);
	if (!otherClients) {
		bool gone = false;
		for (int32 i = 0; i < 50 && !gone; i++) {
			snooze(100000);
			gone = !bridge_thread_exists(team);
		}
		CHECK(gone, "bridge thread gone after the last client left");
	}

	printf("%" B_PRId32 " passed, %" B_PRId32 " failed\n", sPassed, sFailed);
	return sFailed == 0 ? 0 : 1;
}
