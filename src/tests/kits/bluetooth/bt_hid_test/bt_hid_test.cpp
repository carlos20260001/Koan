/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


/*!	Tests a Bluetooth keyboard's way into the system without a keyboard:

	1. SDP: the HID and Device ID records of a Keychron K4 style keyboard,
	   with long strings, through the Bluetooth server's parser
	   (SDPParseAttributeLists, which crashed on any text longer than eight
	   bytes) and its report descriptor lookup; truncated and hostile
	   replies must fail cleanly.
	2. The bluetooth_hid driver: the descriptor makes a keyboard there, the
	   test types into its own window with each press and release written
	   back to back (as Bluetooth delivers them after a busy moment), and
	   checks that every key arrives once, nothing repeats, and the Caps
	   Lock LED comes back out as an output report.

	bt_hid_test [--sdp-only]
*/


#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Application.h>
#include <DataIO.h>
#include <Directory.h>
#include <Entry.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <String.h>
#include <Window.h>

#include <bluetooth/SDPHIDRecord.h>
#include <bluetooth_hid.h>

#include <vector>


typedef std::vector<uint8> Bytes;

static int sFailures = 0;


static void
check(bool condition, const char* what)
{
	printf("%s: %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		sFailures++;
}


// #pragma mark - SDP data


static void
put16(Bytes& data, uint16 value)
{
	data.push_back(value >> 8);
	data.push_back(value & 0xff);
}


static Bytes
u8(uint8 value)
{
	Bytes data;
	data.push_back(SDP_DATA_UINT8);
	data.push_back(value);
	return data;
}


static Bytes
u16(uint16 value)
{
	Bytes data;
	data.push_back(SDP_DATA_UINT16);
	put16(data, value);
	return data;
}


static Bytes
u32(uint32 value)
{
	Bytes data;
	data.push_back(SDP_DATA_UINT32);
	put16(data, value >> 16);
	put16(data, value & 0xffff);
	return data;
}


static Bytes
boolean(bool value)
{
	Bytes data;
	data.push_back(SDP_DATA_BOOL);
	data.push_back(value ? 1 : 0);
	return data;
}


static Bytes
uuid(uint16 value)
{
	Bytes data;
	data.push_back(SDP_DATA_UUID16);
	put16(data, value);
	return data;
}


static Bytes
text(const void* bytes, size_t length)
{
	Bytes data;
	if (length < 256) {
		data.push_back(SDP_DATA_STR8);
		data.push_back(length);
	} else {
		data.push_back(SDP_DATA_STR16);
		put16(data, length);
	}
	data.insert(data.end(), (const uint8*)bytes, (const uint8*)bytes + length);
	return data;
}


static Bytes
text(const char* string)
{
	return text(string, strlen(string));
}


static Bytes
sequence(const std::vector<Bytes>& elements)
{
	Bytes contents;
	for (size_t i = 0; i < elements.size(); i++)
		contents.insert(contents.end(), elements[i].begin(), elements[i].end());
	Bytes data;
	if (contents.size() < 256) {
		data.push_back(SDP_DATA_SEQ8);
		data.push_back(contents.size());
	} else {
		data.push_back(SDP_DATA_SEQ16);
		put16(data, contents.size());
	}
	data.insert(data.end(), contents.begin(), contents.end());
	return data;
}


static void
attribute(std::vector<Bytes>& record, uint16 id, const Bytes& value)
{
	record.push_back(u16(id));
	record.push_back(value);
}


// A keyboard with report IDs, as keyboards that also send media keys have:
// 1 is the boot keyboard layout with five LEDs, 2 consumer control.
static const uint8 kReportDescriptor[] = {
	0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01,
	0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00,
	0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
	0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,
	0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03,
	0x91, 0x01,
	0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xff,
	0x00, 0x05, 0x07, 0x19, 0x00, 0x2a, 0xff, 0x00,
	0x81, 0x00, 0xc0,
	0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x02,
	0x15, 0x00, 0x26, 0xff, 0x03, 0x19, 0x00, 0x2a,
	0xff, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
	0xc0
};


static Bytes
keyboard_attribute_lists()
{
	std::vector<Bytes> hid;
	attribute(hid, 0x0000, u32(0x00010001));
	attribute(hid, 0x0001, sequence({ uuid(0x1124) }));
	attribute(hid, 0x0004, sequence({ sequence({ uuid(0x0100), u16(0x11) }),
		sequence({ uuid(0x0011) }) }));
	attribute(hid, 0x0005, sequence({ uuid(0x1002) }));
	attribute(hid, 0x0006, sequence({ u16(0x656e), u16(0x006a),
		u16(0x0100) }));
	attribute(hid, 0x0009, sequence({ sequence({ uuid(0x1124),
		u16(0x0101) }) }));
	attribute(hid, 0x000d, sequence({ sequence({ sequence({ uuid(0x0100),
		u16(0x13) }), sequence({ uuid(0x0011) }) }) }));
	// long texts, padded with spaces as such keyboards' are
	attribute(hid, 0x0100, text("Keychron K4 Keyboard       "));
	attribute(hid, 0x0101, text("Keyboard                                 "
		"                       "));
	attribute(hid, 0x0102, text("Keychron"));
	attribute(hid, 0x0201, u16(0x0111));
	attribute(hid, 0x0202, u8(0xc0));
	attribute(hid, 0x0203, u8(0x21));
	attribute(hid, 0x0204, boolean(true));
	attribute(hid, 0x0205, boolean(true));
	attribute(hid, 0x0206, sequence({ sequence({ u8(0x22),
		text(kReportDescriptor, sizeof(kReportDescriptor)) }) }));
	attribute(hid, 0x0207, sequence({ sequence({ u16(0x0409),
		u16(0x0100) }) }));
	attribute(hid, 0x020b, u16(0x0101));
	attribute(hid, 0x020c, u16(0x0c80));
	attribute(hid, 0x020d, boolean(false));
	attribute(hid, 0x020e, boolean(true));

	std::vector<Bytes> pnp;
	attribute(pnp, 0x0000, u32(0x00010002));
	attribute(pnp, 0x0001, sequence({ uuid(0x1200) }));
	attribute(pnp, 0x0200, u16(0x0103));
	attribute(pnp, 0x0201, u16(0x3434));
	attribute(pnp, 0x0202, u16(0x0240));
	attribute(pnp, 0x0203, u16(0x0100));
	attribute(pnp, 0x0204, boolean(true));
	attribute(pnp, 0x0205, u16(0x0002));

	return sequence({ sequence(hid), sequence(pnp) });
}


static status_t
parse(const Bytes& data, BMessage& services)
{
	BMallocIO io;
	io.Write(data.data(), data.size());
	return Bluetooth::SDPParseAttributeLists(&io, services);
}


static void
test_sdp(Bytes& descriptor)
{
	Bytes lists = keyboard_attribute_lists();
	BMessage services;
	status_t status = parse(lists, services);
	check(status == B_OK, "a keyboard's SDP records parse");

	BMessage record;
	int32 records = 0;
	bool foundHID = false;
	bool foundPnP = false;
	for (int32 i = 0; services.FindMessage("service", i, &record) == B_OK;
			i++) {
		records++;
		if (Bluetooth::SDPHasServiceClass(record, 0x1200)) {
			foundPnP = record.GetUInt16("0x0201", 0) == 0x3434
				&& record.GetUInt16("0x0202", 0) == 0x0240;
			continue;
		}
		if (!Bluetooth::SDPHasServiceClass(record, 0x1124))
			continue;

		BString name = record.GetString("0x0100", "");
		check(name == "Keychron K4 Keyboard       ",
			"the service name survives intact");
		BString description = record.GetString("0x0101", "");
		check(description.Length() == 64 && description.StartsWith("Keyboard"),
			"the 64-byte description survives intact");

		const void* data;
		ssize_t size;
		const uint8* found;
		size_t foundLength;
		if (record.FindData("0x0206", B_RAW_TYPE, &data, &size) == B_OK
			&& Bluetooth::SDPFindHIDReportDescriptor((const uint8*)data, size,
				found, foundLength)) {
			foundHID = foundLength == sizeof(kReportDescriptor)
				&& memcmp(found, kReportDescriptor, foundLength) == 0;
			descriptor.assign(found, found + foundLength);
		}
		check(record.GetBool("0x0205", false),
			"HIDReconnectInitiate reads as true");
	}
	check(records == 2, "both records are there");
	check(foundHID, "the report descriptor comes out byte for byte");
	check(foundPnP, "the Device ID record gives vendor and product");

	// Every truncation must end with an error or a shorter result, never a
	// crash or a hang.
	int32 truncationErrors = 0;
	for (size_t length = 1; length < lists.size(); length++) {
		Bytes cut(lists.begin(), lists.begin() + length);
		BMessage partial;
		if (parse(cut, partial) != B_OK)
			truncationErrors++;
	}
	printf("     %" B_PRId32 " of %zu truncations refused\n", truncationErrors,
		lists.size() - 1);
	check(truncationErrors > 0, "truncated replies end without a crash");

	// A string claiming 4 GiB, and one claiming more than there is
	Bytes hostile;
	hostile.push_back(SDP_DATA_SEQ8);
	hostile.push_back(10);
	hostile.push_back(SDP_DATA_SEQ8);
	hostile.push_back(8);
	Bytes id = u16(0x0100);
	hostile.insert(hostile.end(), id.begin(), id.end());
	hostile.push_back(SDP_DATA_STR32);
	hostile.push_back(0xff);
	hostile.push_back(0xff);
	hostile.push_back(0xff);
	hostile.push_back(0xf0);
	BMessage refused;
	check(parse(hostile, refused) != B_OK, "a 4 GiB string is refused");
}


// #pragma mark - typing


static const uint8 kKeyboardReport = 1;
static const uint8 kLeftShift = 0x02;


static uint8
usage_for(char c, bool& shift)
{
	shift = false;
	if (c >= 'a' && c <= 'z')
		return 0x04 + (c - 'a');
	if (c >= 'A' && c <= 'Z') {
		shift = true;
		return 0x04 + (c - 'A');
	}
	if (c >= '1' && c <= '9')
		return 0x1e + (c - '1');
	if (c == '0')
		return 0x27;
	if (c == ' ')
		return 0x2c;
	return 0;
}


class KeyWindow : public BWindow {
public:
	KeyWindow()
		:
		BWindow(BRect(100, 100, 600, 300), "bt_hid_test: typing here",
			B_TITLED_WINDOW, B_NOT_RESIZABLE),
		fDowns(0),
		fUps(0),
		fRepeats(0)
	{
		BView* view = new BView(Bounds(), "keys", B_FOLLOW_ALL, 0);
		view->SetViewColor(240, 240, 220);
		AddChild(view);
		view->MakeFocus(true);
	}

	virtual void DispatchMessage(BMessage* message, BHandler* target)
	{
		switch (message->what) {
			case B_KEY_DOWN:
			case B_UNMAPPED_KEY_DOWN:
			{
				if (message->GetInt32("be:key_repeat", 0) > 0)
					fRepeats++;
				else
					fDowns++;
				const char* bytes;
				if (message->what == B_KEY_DOWN
					&& message->FindString("bytes", &bytes) == B_OK
					&& message->GetInt32("be:key_repeat", 0) == 0)
					fTyped << bytes;
				return;
			}
			case B_KEY_UP:
			case B_UNMAPPED_KEY_UP:
				fUps++;
				return;
		}
		BWindow::DispatchMessage(message, target);
	}

	BString		fTyped;
	int32		fDowns;
	int32		fUps;
	int32		fRepeats;
};


struct OutputReader {
	int			fd;
	Bytes		reports[8];
	int32		count;
};


static status_t
read_output(void* data)
{
	OutputReader* reader = (OutputReader*)data;
	uint8 buffer[sizeof(bluetooth_hid_output) + BLUETOOTH_HID_MAX_REPORT];
	while (reader->count < 8) {
		ssize_t length = read(reader->fd, buffer, sizeof(buffer));
		if (length < (ssize_t)sizeof(bluetooth_hid_output))
			break;
		bluetooth_hid_output* header = (bluetooth_hid_output*)buffer;
		uint8* report = buffer + sizeof(bluetooth_hid_output);
		reader->reports[reader->count++].assign(report,
			report + header->length);
	}
	return B_OK;
}


static int32
count_bluetooth_keyboards()
{
	DIR* directory = opendir("/dev/input/keyboard/bluetooth");
	if (directory == NULL)
		return 0;
	int32 count = 0;
	while (dirent* entry = readdir(directory)) {
		if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
			count++;
	}
	closedir(directory);
	return count;
}


static void
write_report(int fd, uint8 modifiers, uint8 key)
{
	uint8 report[9] = { kKeyboardReport, modifiers, 0, key, 0, 0, 0, 0, 0 };
	if (write(fd, report, sizeof(report)) != (ssize_t)sizeof(report))
		printf("     write failed: %s\n", strerror(errno));
}


static void
test_typing(const Bytes& descriptor, KeyWindow* window)
{
	int32 before = count_bluetooth_keyboards();
	int fd = open(BLUETOOTH_HID_CONTROL_PATH, O_RDWR);
	check(fd >= 0, "the bluetooth_hid control device opens");
	if (fd < 0)
		return;

	bluetooth_hid_create create;
	memset(&create, 0, sizeof(create));
	create.version = BLUETOOTH_HID_VERSION;
	create.vendor_id = 0x3434;
	create.product_id = 0x0240;
	create.descriptor_length = descriptor.size();
	create.descriptor = descriptor.data();
	strlcpy(create.name, "bt_hid_test", sizeof(create.name));
	status_t status = ioctl(fd, BLUETOOTH_HID_CREATE, &create, sizeof(create))
		== 0 ? B_OK : errno;
	check(status == B_OK, "the driver takes the report descriptor");
	if (status != B_OK) {
		close(fd);
		return;
	}
	// two: the keys, and the consumer control collection (media keys)
	int32 published = count_bluetooth_keyboards() - before;
	printf("     %" B_PRId32 " keyboard devices published\n", published);
	check(published == 2, "its keyboard and media keys appear in "
		"/dev/input/keyboard/bluetooth");

	// the input server opens new keyboards when devfs tells it
	snooze(2000000);
	window->Lock();
	window->Activate();
	window->Unlock();
	snooze(500000);

	// Each press and its release back to back: what a Bluetooth keyboard's
	// reports look like when the radio was busy for a moment.
	const char* kText = "Hello Bluetooth 2026";
	for (const char* c = kText; *c != '\0'; c++) {
		bool shift;
		uint8 usage = usage_for(*c, shift);
		write_report(fd, shift ? kLeftShift : 0, usage);
		write_report(fd, 0, 0);
	}
	// and a fast burst of the same key
	for (int i = 0; i < 20; i++) {
		bool shift;
		write_report(fd, 0, usage_for('x', shift));
		write_report(fd, 0, 0);
	}
	// longer than the key repeat delay: a lost release would repeat by now
	snooze(1500000);

	BString expected(kText);
	expected << "xxxxxxxxxxxxxxxxxxxx";
	window->Lock();
	BString typed = window->fTyped;
	int32 downs = window->fDowns;
	int32 ups = window->fUps;
	int32 repeats = window->fRepeats;
	window->Unlock();
	printf("     typed \"%s\": %" B_PRId32 " downs, %" B_PRId32 " ups, %"
		B_PRId32 " repeats\n", typed.String(), downs, ups, repeats);
	check(typed == expected, "every key arrives once, in order");
	check(repeats == 0, "no key repeats (no release was lost)");
	check(ups == downs, "every press has its release");

	// The Caps Lock LED: pressing Caps Lock on the keyboard toggles the lock,
	// and the input server sets the LEDs of the keyboard it came from.
	OutputReader reader;
	reader.fd = fd;
	reader.count = 0;
	thread_id thread = spawn_thread(&read_output, "output reports",
		B_NORMAL_PRIORITY, &reader);
	resume_thread(thread);
	snooze(200000);
	int32 openReports = reader.count;
	bool capsWasOn = (modifiers() & B_CAPS_LOCK) != 0;
	const uint8 kCapsLock = 0x39;
	write_report(fd, 0, kCapsLock);
	write_report(fd, 0, 0);
	snooze(500000);
	bool capsToggled = ((modifiers() & B_CAPS_LOCK) != 0) != capsWasOn;
	write_report(fd, 0, kCapsLock);
	write_report(fd, 0, 0);
	snooze(500000);
	bool capsRestored = ((modifiers() & B_CAPS_LOCK) != 0) == capsWasOn;
	check(capsToggled && capsRestored,
		"Caps Lock on the Bluetooth keyboard toggles the lock");

	bool ledOn = false;
	bool ledOff = false;
	for (int32 i = 0; i < reader.count; i++) {
		const Bytes& report = reader.reports[i];
		printf("     output report%s:", i < openReports ? " (on open)" : "");
		for (size_t j = 0; j < report.size(); j++)
			printf(" %02x", report[j]);
		printf("\n");
		if (i < openReports || report.size() != 2 || report[0] != kKeyboardReport)
			continue;
		bool caps = (report[1] & 0x02) != 0;
		if (caps != capsWasOn)
			ledOn = true;
		else if (ledOn)
			ledOff = true;
	}
	check(ledOn && ledOff, "the Caps Lock LED goes out and back");

	// closing the control device removes the keyboard and wakes the reader
	close(fd);
	status_t result;
	wait_for_thread(thread, &result);
	snooze(500000);
	check(count_bluetooth_keyboards() == before,
		"the keyboard goes away with its control device");
}


class TestApp : public BApplication {
public:
	TestApp(const Bytes& descriptor)
		:
		BApplication("application/x-vnd.vos-bt_hid_test"),
		fDescriptor(descriptor)
	{
	}

	virtual void ReadyToRun()
	{
		fWindow = new KeyWindow();
		fWindow->Show();
		thread_id thread = spawn_thread(&_Run, "typing", B_NORMAL_PRIORITY,
			this);
		resume_thread(thread);
	}

private:
	static status_t _Run(void* data)
	{
		TestApp* app = (TestApp*)data;
		test_typing(app->fDescriptor, app->fWindow);
		app->PostMessage(B_QUIT_REQUESTED);
		return B_OK;
	}

	Bytes		fDescriptor;
	KeyWindow*	fWindow;
};


static void
fault(int signal)
{
	// Report a crash (a smashed stack, say) instead of calling the debugger
	// onto the desktop.
	char message[64];
	int length = snprintf(message, sizeof(message),
		"CRASHED: signal %d in the SDP parsing\n", signal);
	write(STDOUT_FILENO, message, length);
	_exit(2);
}


int
main(int argc, char** argv)
{
	static char alternateStack[65536];
	stack_t stack;
	stack.ss_sp = alternateStack;
	stack.ss_size = sizeof(alternateStack);
	stack.ss_flags = 0;
	sigaltstack(&stack, NULL);
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = fault;
	action.sa_flags = SA_ONSTACK;
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGBUS, &action, NULL);
	sigaction(SIGILL, &action, NULL);

	Bytes descriptor;
	test_sdp(descriptor);

	if (argc < 2 || strcmp(argv[1], "--sdp-only") != 0) {
		if (descriptor.empty()) {
			printf("FAIL: no descriptor to type with\n");
			return 1;
		}
		TestApp app(descriptor);
		app.Run();
	}

	printf("%s: %d failure%s\n", sFailures == 0 ? "ALL PASSED" : "FAILED",
		sFailures, sFailures == 1 ? "" : "s");
	return sFailures == 0 ? 0 : 1;
}
