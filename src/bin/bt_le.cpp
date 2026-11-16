/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * bt_le: command line access to Bluetooth Low Energy scanning, pairing and
 * the stored bonds, mainly for diagnosing pairing problems.
 */

#include <Application.h>
#include <Autolock.h>
#include <Locker.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>

#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>
#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/DeviceClass.h>

#include <LEAttributeClient.h>
#include <LEBondStore.h>
#include <LELog.h>
#include <LEPairingSession.h>
#include <bluetoothserver_p.h>

#include <algorithm>
#include <map>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>


using namespace Bluetooth;


static void
Usage()
{
	fprintf(stderr,
		"usage: bt_le scan [seconds]\n"
		"       bt_le pair <address> [public|random]\n"
		"       bt_le services <address> [public|random]\n"
		"       bt_le list\n"
		"       bt_le forget <address> [public|random]\n"
		"       bt_le log [0-3]\n"
		"       bt_le params <min ms> <max ms> [latency] [timeout ms]\n"
		"       bt_le probe <address> [seconds] [interval ms] [latency] "
			"[public|random]\n"
		"       bt_le inquiry [seconds]\n"
		"       bt_le rssi [handle] [count]\n"
		"Addresses are written as AA:BB:CC:DD:EE:FF. Pair a device while it\n"
		"is in pairing mode; the log is %s.\n", LELogFilePath());
}


static bool
ParseAddress(const char* text, uint8 address[6])
{
	unsigned int bytes[6];
	if (sscanf(text, "%x:%x:%x:%x:%x:%x", &bytes[0], &bytes[1], &bytes[2],
			&bytes[3], &bytes[4], &bytes[5]) != 6)
		return false;
	// Text is most significant first; the wire order is the reverse.
	for (int i = 0; i < 6; i++) {
		if (bytes[i] > 0xff)
			return false;
		address[5 - i] = bytes[i];
	}
	return true;
}


static LocalDevice*
GetAdapter(uint8 localAddress[6])
{
	LocalDevice* local = LocalDevice::GetLocalDevice();
	if (local == NULL) {
		fprintf(stderr, "No Bluetooth adapter (is bluetooth_server running?)\n");
		return NULL;
	}
	bdaddr_t address = local->GetBluetoothAddress();
	memcpy(localAddress, address.b, 6);
	return local;
}


struct Advertiser {
	uint8 address[6];
	uint8 type;
	int8 rssi;
	bool connectable;
	std::string name;
	uint16 appearance;
	bool hid;
	uint32 reports;
};


class ScanListener : public BLooper {
public:
	ScanListener()
		:
		BLooper("bt_le scan"),
		fStarted(false),
		fError(false)
	{
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case BT_MSG_LE_SCAN_STARTED:
				fStarted = true;
				break;
			case BT_MSG_LE_SCAN_ERROR:
				fError = true;
				break;
			case BT_MSG_LE_ADVERTISEMENT:
				_Add(message);
				break;
			default:
				BLooper::MessageReceived(message);
		}
	}

	std::vector<Advertiser> Results()
	{
		BAutolock lock(this);
		std::vector<Advertiser> results;
		for (auto& entry : fDevices)
			results.push_back(entry.second);
		return results;
	}

	bool fStarted;
	bool fError;

private:
	void _Add(BMessage* message)
	{
		const uint8* address;
		const uint8* data;
		ssize_t addressLength, dataLength = 0;
		uint8 type = 0, eventType = 0;
		int8 rssi = 0;
		if (message->FindData("address", B_RAW_TYPE, (const void**)&address,
				&addressLength) != B_OK || addressLength != 6)
			return;
		message->FindUInt8("address_type", &type);
		message->FindUInt8("event_type", &eventType);
		message->FindInt8("rssi", &rssi);
		if (message->FindData("data", B_RAW_TYPE, (const void**)&data,
				&dataLength) != B_OK)
			dataLength = 0;

		std::string key((const char*)address, 6);
		key += (char)type;
		Advertiser& device = fDevices[key];
		if (device.reports == 0) {
			memcpy(device.address, address, 6);
			device.type = type;
			device.connectable = false;
			device.appearance = 0;
			device.hid = false;
		}
		device.reports++;
		device.rssi = rssi;
		if (eventType == 0 || eventType == 1)
			device.connectable = true;

		for (ssize_t offset = 0; offset + 1 < dataLength;) {
			uint8 length = data[offset];
			if (length == 0 || offset + 1 + length > dataLength)
				break;
			uint8 field = data[offset + 1];
			const uint8* value = data + offset + 2;
			size_t valueLength = length - 1;
			if (field == 0x09 || (field == 0x08 && device.name.empty()))
				device.name.assign((const char*)value, valueLength);
			else if (field == 0x19 && valueLength >= 2)
				device.appearance = value[0] | (value[1] << 8);
			else if (field == 0x02 || field == 0x03) {
				for (size_t i = 0; i + 1 < valueLength; i += 2) {
					if ((value[i] | (value[i + 1] << 8)) == 0x1812)
						device.hid = true;
				}
			}
			offset += 1 + length;
		}
	}

	std::map<std::string, Advertiser> fDevices;
};


static int
Scan(int seconds)
{
	uint8 local[6];
	LocalDevice* adapter = GetAdapter(local);
	if (adapter == NULL)
		return 1;
	BMessenger server(BLUETOOTH_SIGNATURE);
	ScanListener* listener = new ScanListener();
	listener->Run();
	BMessenger listenerMessenger(listener);

	BMessage request(BT_MSG_LE_SCAN_START), reply;
	request.AddInt32("hci_id", adapter->ID());
	request.AddMessenger("listener", listenerMessenger);
	status_t status = server.SendMessage(&request, &reply, 5000000, 5000000);
	int32 result = B_ERROR;
	if (status == B_OK)
		reply.FindInt32("status", &result);
	if (status != B_OK || result != B_OK) {
		fprintf(stderr, "Could not start the LE scan: %s\n",
			strerror(status != B_OK ? status : result));
		return 1;
	}
	printf("Scanning for %d seconds...\n", seconds);
	snooze((bigtime_t)seconds * 1000000);

	BMessage stop(BT_MSG_LE_SCAN_STOP);
	stop.AddInt32("hci_id", adapter->ID());
	stop.AddMessenger("listener", listenerMessenger);
	server.SendMessage(&stop, &reply, 5000000, 5000000);
	snooze(200000);

	std::vector<Advertiser> results = listener->Results();
	if (listener->Lock())
		listener->Quit();
	std::sort(results.begin(), results.end(),
		[](const Advertiser& a, const Advertiser& b) { return a.rssi > b.rssi; });
	printf("%-17s %-6s %5s  %-4s %-8s %s\n", "address", "type", "rssi", "conn",
		"hint", "name");
	for (const Advertiser& device : results) {
		char text[18];
		const char* hint = "";
		if (device.appearance == 0x03c2)
			hint = "mouse";
		else if (device.appearance == 0x03c1)
			hint = "keyboard";
		else if (device.hid)
			hint = "HID";
		printf("%-17s %-6s %5d  %-4s %-8s %s\n",
			LEAddressString(device.address, text),
			device.type == 0 ? "public" : "random", device.rssi,
			device.connectable ? "yes" : "no", hint, device.name.c_str());
	}
	if (listener->fError)
		fprintf(stderr, "The scan reported an error; see the log.\n");
	return 0;
}


class ProgressPrinter : public BLooper {
public:
	ProgressPrinter() : BLooper("bt_le progress") {}

	void MessageReceived(BMessage* message) override
	{
		if (message->what != LE_PAIRING_PROGRESS_MESSAGE) {
			BLooper::MessageReceived(message);
			return;
		}
		static const char* kStages[] = { "connect", "bond lookup",
			"pairing", "security channel", "pairing request",
			"pairing response", "confirm", "random", "encryption",
			"key distribution", "saving bond", "HID discovery", "complete",
			"cleanup" };
		int32 stage = 0;
		message->FindInt32("stage", &stage);
		const char* detail = message->FindString("detail");
		printf("  [%s]%s%s\n", stage >= 0 && stage < (int32)B_COUNT_OF(kStages)
			? kStages[stage] : "?", detail != NULL ? " " : "",
			detail != NULL ? detail : "");
		fflush(stdout);
	}
};


static int
Pair(const uint8 peer[6], uint8 type)
{
	uint8 local[6];
	LocalDevice* adapter = GetAdapter(local);
	if (adapter == NULL)
		return 1;
	char text[18];
	printf("Pairing %s (%s address). Keep the device in pairing mode.\n",
		LEAddressString(peer, text), type == 0 ? "public" : "random");
	ProgressPrinter* printer = new ProgressPrinter();
	printer->Run();
	BMessenger progress(printer);
	LEPairingResult result = PairLEDevice(adapter->ID(), local, peer, type,
		&progress);
	snooze(100000);
	if (printer->Lock())
		printer->Quit();
	if (result.status != B_OK) {
		printf("Pairing failed: %s\n", result.detail[0] != '\0'
			? result.detail : strerror(result.status));
		printf("Details: %s\n", LELogFilePath());
		return 1;
	}
	printf("Paired%s. Encrypted: %s, bonded: %s.\n",
		result.hidMouseReady ? " as a mouse" : "",
		result.encrypted ? "yes" : "no", result.bonded ? "yes" : "no");
	if (!result.hidMouseReady && result.detail[0] != '\0')
		printf("Note: %s\n", result.detail);
	return 0;
}


class LinkListener : public BLooper {
public:
	LinkListener()
		:
		BLooper("bt_le link"),
		fSemaphore(create_sem(0, "bt_le link"))
	{
	}

	~LinkListener() override
	{
		delete_sem(fSemaphore);
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case BT_MSG_LE_CONNECTED:
			case BT_MSG_LE_CONNECT_FAILED:
			case BT_MSG_LE_DISCONNECTED:
			{
				int32 status = B_ERROR;
				message->FindInt32("status", &status);
				fLast = message->what;
				fStatus = status;
				release_sem(fSemaphore);
				break;
			}
			default:
				BLooper::MessageReceived(message);
		}
	}

	status_t Wait(bigtime_t timeout)
	{
		status_t status = acquire_sem_etc(fSemaphore, 1, B_RELATIVE_TIMEOUT,
			timeout);
		if (status != B_OK)
			return status;
		return fLast == BT_MSG_LE_CONNECTED ? B_OK
			: (fStatus != B_OK ? fStatus : B_ERROR);
	}

private:
	sem_id fSemaphore;
	uint32 fLast;
	int32 fStatus;
};


static int
Services(const uint8 peer[6], uint8 type)
{
	uint8 local[6];
	LocalDevice* adapter = GetAdapter(local);
	if (adapter == NULL)
		return 1;
	BMessenger server(BLUETOOTH_SIGNATURE);
	LinkListener* listener = new LinkListener();
	listener->Run();
	BMessenger listenerMessenger(listener);
	char text[18];
	printf("Connecting to %s (%s, no pairing)...\n", LEAddressString(peer, text),
		type == 0 ? "public" : "random");

	BMessage request(BT_MSG_LE_CONNECT), reply;
	request.AddInt32("hci_id", adapter->ID());
	request.AddMessenger("listener", listenerMessenger);
	request.AddData("address", B_RAW_TYPE, peer, 6);
	request.AddUInt8("address_type", type);
	int32 result = B_ERROR;
	if (server.SendMessage(&request, &reply, 5000000, 5000000) == B_OK)
		reply.FindInt32("status", &result);
	if (result != B_OK) {
		fprintf(stderr, "Connection request refused: %s\n", strerror(result));
		return 1;
	}
	status_t status = listener->Wait(20000000);
	int exitCode = 1;
	if (status != B_OK) {
		fprintf(stderr, "Not connected: %s\n", strerror(status));
		BMessage cancel(BT_MSG_LE_CONNECT_CANCEL);
		cancel.AddInt32("hci_id", adapter->ID());
		cancel.AddMessenger("listener", listenerMessenger);
		server.SendMessage(&cancel, &reply, 5000000, 5000000);
		snooze(1000000);
	} else {
		printf("Connected.\n");
		bdaddr_t address;
		memcpy(address.b, peer, 6);
		LEAttributeClient client;
		status = client.Connect(address);
		std::vector<LEPrimaryService> services;
		if (status == B_OK)
			status = client.DiscoverPrimaryServices(services);
		if (status != B_OK)
			fprintf(stderr, "Service discovery failed: %s\n", strerror(status));
		else {
			for (const LEPrimaryService& service : services) {
				printf("  service %#06x handles %#06x-%#06x\n", service.uuid16,
					service.startHandle, service.endHandle);
			}
			printf("%zu primary services.\n", services.size());
			exitCode = 0;
		}
		client.Disconnect();
		BMessage disconnect(BT_MSG_LE_DISCONNECT);
		disconnect.AddInt32("hci_id", adapter->ID());
		disconnect.AddMessenger("listener", listenerMessenger);
		server.SendMessage(&disconnect, &reply, 5000000, 5000000);
		listener->Wait(3000000);
	}
	if (listener->Lock())
		listener->Quit();
	return exitCode;
}


/*!	Times a stream of notifications from a test peer (tools/cubie-a7s/
	lepeer.py, a BlueZ peripheral on a Linux host, sends a sequence number
	and its clock every few milliseconds, more often than any connection
	interval, so every connection event has something to carry). Arrivals
	less than half an interval apart came in the same connection event; the
	gaps between events, in intervals, show the events the link lost. Raw
	arrivals go to $BT_LE_PROBE_CSV if set.
*/
static int
ProbeNotifications(LEAttributeClient& client, uint16 handle, const char* text,
	int32 seconds, float intervalMs, int32 latency)
{
	struct Arrival {
		bigtime_t when;
		uint32 sequence;
		uint32 stamp;
	};
	std::vector<Arrival> arrivals;
	arrivals.reserve(seconds * 1000);
	const bigtime_t end = system_time() + seconds * 1000000LL;
	while (system_time() < end) {
		uint16 from;
		std::vector<uint8> value;
		status_t status = client.ReadNotification(from, value);
		if (status == B_TIMED_OUT)
			continue;
		if (status != B_OK) {
			fprintf(stderr, "Link lost after %zu notifications: %s\n",
				arrivals.size(), strerror(status));
			break;
		}
		if (from != handle || value.size() < 8)
			continue;
		Arrival arrival;
		arrival.when = system_time();
		memcpy(&arrival.sequence, value.data(), 4);
		memcpy(&arrival.stamp, value.data() + 4, 4);
		arrival.sequence = B_LENDIAN_TO_HOST_INT32(arrival.sequence);
		arrival.stamp = B_LENDIAN_TO_HOST_INT32(arrival.stamp);
		arrivals.push_back(arrival);
	}
	if (arrivals.size() < 2) {
		fprintf(stderr, "No notification stream from %s.\n", text);
		return 1;
	}

	const char* csvPath = getenv("BT_LE_PROBE_CSV");
	if (csvPath != NULL) {
		FILE* csv = fopen(csvPath, "w");
		if (csv != NULL) {
			for (const Arrival& arrival : arrivals) {
				fprintf(csv, "%" B_PRIdBIGTIME ",%" B_PRIu32 ",%" B_PRIu32
					"\n", arrival.when, arrival.sequence, arrival.stamp);
			}
			fclose(csv);
		}
	}

	const float unit = intervalMs > 0 ? intervalMs : 11.25f;
	const bigtime_t sameEvent = (bigtime_t)(unit * 500);
	std::vector<bigtime_t> eventGaps;
	bigtime_t eventStart = arrivals[0].when;
	bigtime_t previous = arrivals[0].when;
	uint32 lost = 0;
	for (size_t i = 1; i < arrivals.size(); i++) {
		if (arrivals[i].sequence != arrivals[i - 1].sequence + 1)
			lost += arrivals[i].sequence - arrivals[i - 1].sequence - 1;
		if (arrivals[i].when - previous > sameEvent) {
			eventGaps.push_back(arrivals[i].when - eventStart);
			eventStart = arrivals[i].when;
		}
		previous = arrivals[i].when;
	}
	// Queueing delay: arrival minus the peer's send time, against the best
	// seen (the clocks differ by a constant).
	std::vector<int64> delays;
	int64 best = INT64_MAX;
	uint32 base = arrivals[0].stamp;
	for (const Arrival& arrival : arrivals) {
		int64 delay = arrival.when - (int64)(uint32)(arrival.stamp - base);
		delays.push_back(delay);
		best = std::min(best, delay);
	}
	for (int64& delay : delays)
		delay -= best;
	std::sort(delays.begin(), delays.end());

	int32 buckets[6] = {};
	for (bigtime_t gap : eventGaps) {
		int32 events = (int32)(gap / 1000.0f / unit + 0.5f);
		buckets[std::max((int32)1, std::min(events, (int32)5))]++;
	}
	const size_t gaps = std::max(eventGaps.size(), (size_t)1);
	const size_t n = delays.size();
	char summary[640];
	snprintf(summary, sizeof(summary), "probe %s: %zu notifications (%" B_PRIu32
		" lost) in %" B_PRId32 " s at %.2f ms latency %" B_PRId32 ": %zu "
		"connection events with data, gaps of 1/2/3/4/5+ intervals %.1f/%.1f/"
		"%.1f/%.1f/%.1f%%; delay median %.1f, 90%% %.1f, 99%% %.1f, max %.1f "
		"ms", text, n, lost, seconds, unit, latency, eventGaps.size() + 1,
		100.0 * buckets[1] / gaps, 100.0 * buckets[2] / gaps,
		100.0 * buckets[3] / gaps, 100.0 * buckets[4] / gaps,
		100.0 * buckets[5] / gaps, delays[n / 2] / 1000.0,
		delays[n * 9 / 10] / 1000.0, delays[n * 99 / 100] / 1000.0,
		delays[n - 1] / 1000.0);
	printf("%s\n", summary);
	LELog(LE_LOG_INFO, "probe", "%s", summary);
	return 0;
}


/*!	Measures an LE link without a person: holds a connection to a device
	that lets anyone read an attribute (most do: the GAP device name), asks
	for the connection parameters under test and times back-to-back reads.
	A read takes one connection event to send and usually the next to answer;
	connection events the controller gives to something else (a Classic
	inquiry, audio, Wi-Fi) show up as slower reads.
*/
static int
Probe(const uint8 peer[6], uint8 type, int32 seconds, float intervalMs,
	int32 latency)
{
	uint8 local[6];
	LocalDevice* adapter = GetAdapter(local);
	if (adapter == NULL)
		return 1;
	BMessenger server(BLUETOOTH_SIGNATURE);
	LinkListener* listener = new LinkListener();
	listener->Run();
	BMessenger listenerMessenger(listener);
	char text[18];
	LEAddressString(peer, text);

	BMessage request(BT_MSG_LE_CONNECT), reply;
	request.AddInt32("hci_id", adapter->ID());
	request.AddMessenger("listener", listenerMessenger);
	request.AddData("address", B_RAW_TYPE, peer, 6);
	request.AddUInt8("address_type", type);
	int32 result = B_ERROR;
	if (server.SendMessage(&request, &reply, 5000000, 5000000) == B_OK)
		reply.FindInt32("status", &result);
	if (result != B_OK) {
		fprintf(stderr, "Connection request refused: %s\n", strerror(result));
		return 1;
	}
	status_t status = listener->Wait(20000000);
	if (status != B_OK) {
		fprintf(stderr, "Not connected to %s: %s\n", text, strerror(status));
		BMessage cancel(BT_MSG_LE_CONNECT_CANCEL);
		cancel.AddInt32("hci_id", adapter->ID());
		cancel.AddMessenger("listener", listenerMessenger);
		server.SendMessage(&cancel, &reply, 5000000, 5000000);
		return 1;
	}

	int exitCode = 1;
	if (intervalMs > 0) {
		// Before discovery, which then runs at the new interval too.
		BMessage update(BT_MSG_LE_UPDATE_CONNECTION);
		update.AddInt32("hci_id", adapter->ID());
		uint16 interval = (uint16)(intervalMs / 1.25 + 0.5);
		update.AddUInt16("interval_min", interval);
		update.AddUInt16("interval_max", interval);
		update.AddUInt16("latency", (uint16)latency);
		update.AddUInt16("timeout", 300);
		server.SendMessage(&update, &reply, 5000000, 5000000);
		snooze(500000);
	}
	bdaddr_t address;
	memcpy(address.b, peer, 6);
	LEAttributeClient client;
	status = client.Connect(address);

	// Something to read: the device name, else any readable value. A
	// characteristic that notifies, in a vendor (128-bit) service, is better
	// still: a test peer sending a stream stands in for a mouse.
	uint16 handle = 0;
	uint16 notifyHandle = 0;
	uint16 notifyEnd = 0;
	std::vector<LEPrimaryService> services;
	if (status == B_OK)
		status = client.DiscoverPrimaryServices(services);
	for (size_t i = 0; status == B_OK && i < services.size(); i++) {
		std::vector<LECharacteristic> characteristics;
		if (client.DiscoverCharacteristics(services[i].startHandle,
				services[i].endHandle, characteristics) != B_OK)
			continue;
		for (const LECharacteristic& characteristic : characteristics) {
			if (services[i].uuid16 == 0 && notifyHandle == 0
				&& (characteristic.properties & 0x10) != 0) {
				notifyHandle = characteristic.valueHandle;
				notifyEnd = services[i].endHandle;
			}
			if ((characteristic.properties & 0x02) == 0)
				continue;
			if (handle == 0 || (characteristic.uuid16 == 0x2a00
					&& notifyHandle == 0))
				handle = characteristic.valueHandle;
		}
	}
	uint16 configuration = 0;
	if (notifyHandle != 0
		&& client.FindClientConfiguration(notifyHandle + 1, notifyEnd,
			configuration) != B_OK)
		notifyHandle = 0;

	if (status == B_OK && notifyHandle != 0) {
		const uint8 enable[2] = { 1, 0 };
		status = client.WriteAttribute(configuration, enable, 2);
		if (status == B_OK) {
			exitCode = ProbeNotifications(client, notifyHandle, text, seconds,
				intervalMs, latency);
			const uint8 disable[2] = { 0, 0 };
			client.WriteAttribute(configuration, disable, 2);
		} else
			fprintf(stderr, "Subscribing failed: %s\n", strerror(status));
	} else if (status != B_OK || handle == 0) {
		fprintf(stderr, "Nothing to read on %s: %s\n", text,
			strerror(status != B_OK ? status : B_ENTRY_NOT_FOUND));
	} else {
		std::vector<bigtime_t> times;
		int32 failures = 0;
		const bigtime_t end = system_time() + seconds * 1000000LL;
		while (system_time() < end && failures < 5) {
			std::vector<uint8> value;
			bigtime_t start = system_time();
			if (client.ReadAttribute(handle, value) != B_OK) {
				failures++;
				continue;
			}
			times.push_back(system_time() - start);
		}
		if (times.empty())
			fprintf(stderr, "No read completed.\n");
		else {
			std::sort(times.begin(), times.end());
			size_t n = times.size();
			bigtime_t total = 0;
			for (bigtime_t t : times)
				total += t;
			const float unit = intervalMs > 0 ? intervalMs : 11.25f;
			int32 buckets[6] = {};
			for (bigtime_t t : times) {
				int32 events = (int32)(t / 1000.0f / unit + 0.5f);
				buckets[std::min(events, (int32)5)]++;
			}
			char summary[512];
			snprintf(summary, sizeof(summary), "probe %s: %zu reads in %"
				B_PRId32 " s at %.2f ms latency %" B_PRId32 ": mean %.1f ms, "
				"median %.1f, 90%% %.1f, 99%% %.1f, max %.1f ms; reads taking "
				"<=1/2/3/4/5+ intervals: %" B_PRId32 "/%" B_PRId32 "/%" B_PRId32
				"/%" B_PRId32 "/%" B_PRId32 "", text, n, seconds, unit,
				latency, total / 1000.0 / n, times[n / 2] / 1000.0,
				times[n * 9 / 10] / 1000.0, times[n * 99 / 100] / 1000.0,
				times[n - 1] / 1000.0, buckets[0] + buckets[1], buckets[2],
				buckets[3], buckets[4], buckets[5]);
			printf("%s\n", summary);
			LELog(LE_LOG_INFO, "probe", "%s", summary);
			exitCode = 0;
		}
	}

	client.Disconnect();
	BMessage disconnect(BT_MSG_LE_DISCONNECT);
	disconnect.AddInt32("hci_id", adapter->ID());
	disconnect.AddMessenger("listener", listenerMessenger);
	server.SendMessage(&disconnect, &reply, 5000000, 5000000);
	listener->Wait(3000000);
	if (listener->Lock())
		listener->Quit();
	return exitCode;
}


class InquiryListener : public DiscoveryListener {
public:
	virtual void DeviceDiscovered(RemoteDevice* device, DeviceClass deviceClass)
	{
		// (the listener keeps the device in its list, to recognize the
		// device's next replies: not ours to delete)
		BString major;
		deviceClass.GetMajorDeviceClass(major);
		printf("%s  class %06" B_PRIx32 " (%s)\n",
			bdaddrUtils::ToString(device->GetBluetoothAddress()).String(),
			deviceClass.Record(), major.String());
	}
	virtual void InquiryCompleted(int discType) {}
	virtual void InquiryStarted(status_t status) {}
};


static int
Inquiry(int32 seconds)
{
	uint8 local[6];
	LocalDevice* adapter = GetAdapter(local);
	if (adapter == NULL)
		return 1;
	DiscoveryAgent* agent = adapter->GetDiscoveryAgent();
	InquiryListener* listener = new InquiryListener();
		// (DiscoveryListener runs itself)
	// in 1.28 s units, as Classic inquiries are
	int32 length = std::max((int32)1, std::min((int32)(seconds / 1.28f + 0.5f),
		(int32)48));
	status_t status = agent->StartInquiry(BT_GIAC, listener, length);
	printf("inquiry of %.2f s: %s\n", length * 1.28f, strerror(status));
	snooze((bigtime_t)(length * 1280000LL + 500000));
	return status == B_OK ? 0 : 1;
}


static int
List()
{
	char directory[1024];
	if (DefaultLEBondDirectory(directory, sizeof(directory)) != B_OK)
		return 1;
	std::vector<LEHIDMouseDevice> mice;
	status_t status = ListLEHIDMice(directory, mice);
	if (status != B_OK && status != B_ENTRY_NOT_FOUND) {
		fprintf(stderr, "Cannot read %s: %s\n", directory, strerror(status));
		return 1;
	}
	if (mice.empty())
		printf("No paired Low Energy mice.\n");
	for (const LEHIDMouseDevice& mouse : mice) {
		char peer[18], local[18];
		printf("%s (%s) on adapter %s\n", LEAddressString(mouse.peerAddress,
			peer), mouse.peerAddressType == 0 ? "public" : "random",
			LEAddressString(mouse.localAddress, local));
	}
	return 0;
}


static int
Forget(const uint8 peer[6], uint8 type)
{
	uint8 local[6];
	LocalDevice* adapter = GetAdapter(local);
	if (adapter == NULL)
		return 1;
	char directory[1024];
	if (DefaultLEBondDirectory(directory, sizeof(directory)) != B_OK)
		return 1;
	status_t status = RemoveLEBond(directory, local, 0, peer, type);
	char text[18];
	if (status != B_OK) {
		fprintf(stderr, "Could not forget %s: %s\n", LEAddressString(peer, text),
			strerror(status));
		return 1;
	}
	printf("Forgot %s.\n", LEAddressString(peer, text));
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		Usage();
		return 1;
	}
	BApplication application("application/x-vnd.vos-bt_le");
	const char* command = argv[1];
	if (strcmp(command, "scan") == 0)
		return Scan(argc > 2 ? atoi(argv[2]) : 10);
	if (strcmp(command, "list") == 0)
		return List();
	if (strcmp(command, "log") == 0) {
		if (argc > 2) {
			status_t status = LESetLogLevel(atoi(argv[2]));
			if (status != B_OK) {
				fprintf(stderr, "Cannot set the level: %s\n", strerror(status));
				return 1;
			}
		}
		printf("level %" B_PRId32 " (0 errors, 1 steps, 2 debug + kernel, "
			"3 packets)\nlog: %s\n", LELogLevel(), LELogFilePath());
		return 0;
	}
	if (strcmp(command, "rssi") == 0) {
		// bt_le rssi [handle] [count]: the LE link's unless a handle is
		// given, twice a second; the values go to the log.
		uint8 local[6];
		LocalDevice* adapter = GetAdapter(local);
		if (adapter == NULL)
			return 1;
		int32 count = argc > 3 ? atoi(argv[3]) : 1;
		status_t status = B_OK;
		for (int32 i = 0; i < count && status == B_OK; i++) {
			BMessage request(BT_MSG_READ_RSSI), reply;
			request.AddInt32("hci_id", adapter->ID());
			if (argc > 2 && strcmp(argv[2], "le") != 0)
				request.AddUInt16("handle", (uint16)strtol(argv[2], NULL, 0));
			status = BMessenger(BLUETOOTH_SIGNATURE).SendMessage(&request,
				&reply, 5000000, 5000000);
			if (status == B_OK)
				status = reply.GetInt32("status", B_ERROR);
			if (i + 1 < count)
				snooze(500000);
		}
		printf("RSSI requested: %s (the values are in the log)\n",
			strerror(status));
		return status == B_OK ? 0 : 1;
	}
	if (strcmp(command, "inquiry") == 0)
		return Inquiry(argc > 2 ? atoi(argv[2]) : 5);
	if (strcmp(command, "probe") == 0 && argc > 2) {
		// bt_le probe <address> [seconds] [interval ms] [latency] [type]
		uint8 address[6];
		if (!ParseAddress(argv[2], address)) {
			Usage();
			return 1;
		}
		uint8 type = (address[5] & 0xc0) == 0xc0 ? 1 : 0;
		if (argc > 6)
			type = strcmp(argv[6], "public") == 0 ? 0 : 1;
		return Probe(address, type, argc > 3 ? atoi(argv[3]) : 20,
			argc > 4 ? atof(argv[4]) : 0, argc > 5 ? atoi(argv[5]) : 0);
	}
	if (strcmp(command, "params") == 0 && argc > 3) {
		// bt_le params <min ms> <max ms> [latency] [timeout ms]: kept on
		// the link until it closes; latency and timeout stay the link's
		// unless given.
		uint8 local[6];
		LocalDevice* adapter = GetAdapter(local);
		if (adapter == NULL)
			return 1;
		BMessage request(BT_MSG_LE_UPDATE_CONNECTION);
		request.AddInt32("hci_id", adapter->ID());
		request.AddUInt16("interval_min", (uint16)(atof(argv[2]) / 1.25 + 0.5));
		request.AddUInt16("interval_max", (uint16)(atof(argv[3]) / 1.25 + 0.5));
		if (argc > 4)
			request.AddUInt16("latency", (uint16)atoi(argv[4]));
		if (argc > 5)
			request.AddUInt16("timeout", (uint16)(atoi(argv[5]) / 10));
		request.AddBool("keep_interval", true);
		BMessage reply;
		status_t status = BMessenger(BLUETOOTH_SIGNATURE).SendMessage(&request,
			&reply, 5000000, 5000000);
		if (status == B_OK)
			status = reply.GetInt32("status", B_ERROR);
		printf("connection update: %s (the result is in the log)\n",
			strerror(status));
		return status == B_OK ? 0 : 1;
	}
	if ((strcmp(command, "pair") == 0 || strcmp(command, "forget") == 0
			|| strcmp(command, "services") == 0)
		&& argc > 2) {
		uint8 address[6];
		if (!ParseAddress(argv[2], address)) {
			Usage();
			return 1;
		}
		// Most mice use a random static address (top two bits set).
		uint8 type = (address[5] & 0xc0) == 0xc0 ? 1 : 0;
		if (argc > 3)
			type = strcmp(argv[3], "public") == 0 ? 0 : 1;
		if (command[0] == 's')
			return Services(address, type);
		return command[0] == 'p' ? Pair(address, type) : Forget(address, type);
	}
	Usage();
	return 1;
}
