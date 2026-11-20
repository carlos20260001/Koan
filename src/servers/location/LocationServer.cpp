/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	The system's location service: programs ask it where the computer is
	(LOCATION_GET_POSITION, <location_server.h>) instead of each finding out
	for itself. The position comes from the settings when the user entered
	one, else from a geolocation service (BeaconDB by default, which speaks
	Mozilla Location Service's protocol) given the Wi-Fi networks in range,
	or from the network address when there are too few of them. Positions
	are kept for a while, and the settings can switch the service off.
	See docs/airos/LOCATION.md.
*/


#include "LocationServer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <Autolock.h>
#include <DataIO.h>
#include <FindDirectory.h>
#include <HttpHeaders.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <Json.h>
#include <NetworkAddress.h>
#include <NetworkDevice.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <Path.h>
#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <driver_settings.h>

#include <location_server.h>

#ifdef __VOS__
#	include "GeoClue.h"
#endif


using namespace BPrivate::Network;


static const char* kDefaultService = "https://api.beacondb.net/v1/geolocate";
static const bigtime_t kDefaultMaxAge = 60000000;
static const bigtime_t kRequestTimeout = 20000000;
static const int32 kMinimumNetworks = 2;
	// what the geolocation services take for a Wi-Fi position


LocationServer::LocationServer(status_t* _error)
	:
	BServer(LOCATION_SERVER_SIGNATURE, false, _error),
	fLock("location"),
	fEnabled(true),
	fHasFixed(false)
{
	fCached.time = 0;
	fFixed.time = 0;
}


void
LocationServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case LOCATION_GET_POSITION:
		{
			// a lookup takes seconds: answer on a thread of its own
			BMessage* request = DetachCurrentMessage();
			thread_id thread = spawn_thread(_RequestEntry, "location request",
				B_NORMAL_PRIORITY, request);
			if (thread < 0) {
				BMessage reply(B_REPLY);
				reply.AddInt32("status", thread);
				request->SendReply(&reply);
				delete request;
			} else
				resume_thread(thread);
			break;
		}

		default:
			BServer::MessageReceived(message);
	}
}


/*static*/ status_t
LocationServer::_RequestEntry(void* data)
{
	BMessage* request = (BMessage*)data;
	((LocationServer*)be_app)->_HandleRequest(request);
	delete request;
	return B_OK;
}


void
LocationServer::_HandleRequest(BMessage* request)
{
	bigtime_t maxAge;
	if (request->FindInt64("max_age", &maxAge) != B_OK || maxAge < 0)
		maxAge = kDefaultMaxAge;

	Position position;
	status_t status = _Position(maxAge, position);

	BMessage reply(B_REPLY);
	reply.AddInt32("status", status);
	if (status == B_OK) {
		reply.AddDouble("latitude", position.latitude);
		reply.AddDouble("longitude", position.longitude);
		reply.AddDouble("accuracy", position.accuracy);
		reply.AddString("source", position.source);
		reply.AddInt64("time", position.time);
	}
	request->SendReply(&reply);
}


status_t
LocationServer::_Position(bigtime_t maxAge, Position& _position)
{
	BAutolock locker(fLock);

	_LoadSettings();
	if (!fEnabled)
		return B_NOT_ALLOWED;
	if (fHasFixed) {
		_position = fFixed;
		_position.time = real_time_clock_usecs();
		return B_OK;
	}

	// a request that waited for a lookup under way gets its result
	if (fCached.time != 0 && real_time_clock_usecs() - fCached.time <= maxAge) {
		_position = fCached;
		return B_OK;
	}

#ifdef __VOS__
	// GeoClue first: it also knows a GPS or a modem, and it may be set up
	// to use another service. Without it, the server looks up the
	// position itself as on air/OS.
	double latitude, longitude, accuracy;
	if (geoclue_position(kRequestTimeout, latitude, longitude, accuracy)
			== B_OK) {
		_position.latitude = latitude;
		_position.longitude = longitude;
		_position.accuracy = accuracy;
		_position.source = "geoclue";
		_position.time = real_time_clock_usecs();
		fCached = _position;
		return B_OK;
	}
#endif

	status_t status = _LookUp(_position);
	if (status == B_OK)
		fCached = _position;
	return status;
}


/*!	Asks the geolocation service, with the Wi-Fi networks in range: their
	hardware addresses, leaving out locally administered (random) ones and
	networks named "*_nomap", whose owners asked not to be mapped.
*/
status_t
LocationServer::_LookUp(Position& _position)
{
	BString networks;
	int32 networkCount = 0;
	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		BNetworkDevice device(interface.Name());
		if (!device.IsWireless())
			continue;
		wireless_network* found = NULL;
		uint32 foundCount = 0;
		if (device.GetNetworks(found, foundCount) != B_OK)
			continue;
		for (uint32 i = 0; i < foundCount; i++) {
			const wireless_network& network = found[i];
			const uint8* address = network.address.LinkLevelAddress();
			if (network.address.LinkLevelAddressLength() != 6
				|| (address[0] & 0x02) != 0)
				continue;
			size_t nameLength = strnlen(network.name, sizeof(network.name));
			if (nameLength >= 6
				&& strncmp(network.name + nameLength - 6, "_nomap", 6) == 0)
				continue;
			char text[18];
			snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
				address[0], address[1], address[2], address[3], address[4],
				address[5]);
			if (networks.FindFirst(text) >= 0)
				continue;
			networks << (networkCount > 0 ? "," : "")
				<< "{\"macAddress\":\"" << text << "\"}";
			networkCount++;
		}
		delete[] found;
	}

	BString body("{\"considerIp\":true");
	if (networkCount >= kMinimumNetworks)
		body << ",\"wifiAccessPoints\":[" << networks << "]";
	body << "}";

	BUrl url(fService.String(), true);
	BMallocIO output;
	BUrlRequest* request = BUrlProtocolRoster::MakeRequest(url, &output, NULL);
	BHttpRequest* http = dynamic_cast<BHttpRequest*>(request);
	if (http == NULL) {
		delete request;
		syslog(LOG_ERR, "location_server: cannot ask %s", fService.String());
		return B_NOT_SUPPORTED;
	}
	BMallocIO* input = new BMallocIO;
	input->Write(body.String(), body.Length());
	input->Seek(0, SEEK_SET);
	http->SetMethod(B_HTTP_POST);
	BHttpHeaders headers;
	headers.AddHeader("Content-Type", "application/json");
	http->SetHeaders(headers);
	http->SetUserAgent("air/OS location_server");
	http->AdoptInputData(input, body.Length());
	http->SetStopOnError(true);

	thread_id thread = request->Run();
	if (thread < 0) {
		delete request;
		return thread;
	}
	bigtime_t deadline = system_time() + kRequestTimeout;
	while (wait_for_thread_etc(thread, B_ABSOLUTE_TIMEOUT, deadline, NULL)
			== B_TIMED_OUT) {
		request->Stop();
	}
	status_t status = request->Status();
	const BHttpResult* result = dynamic_cast<const BHttpResult*>(
		&request->Result());
	int32 code = result != NULL ? result->StatusCode() : 0;
	delete request;
	if (status == B_OK && code != 200)
		status = B_ERROR;
	if (status != B_OK) {
		syslog(LOG_NOTICE, "location_server: %s answered %" B_PRId32 " (%s)",
			fService.String(), code, strerror(status));
		return status;
	}

	BMessage answer;
	BString text((const char*)output.Buffer(), output.BufferLength());
	BMessage location;
	double latitude, longitude, accuracy;
	if (BJson::Parse(text, answer) != B_OK
		|| answer.FindMessage("location", &location) != B_OK
		|| location.FindDouble("lat", &latitude) != B_OK
		|| location.FindDouble("lng", &longitude) != B_OK
		|| answer.FindDouble("accuracy", &accuracy) != B_OK) {
		syslog(LOG_NOTICE, "location_server: no position in the answer");
		return B_BAD_DATA;
	}

	_position.latitude = latitude;
	_position.longitude = longitude;
	_position.accuracy = accuracy;
	// the service says when it used the network address rather than the
	// networks
	_position.source = (answer.HasString("fallback")
		|| networkCount < kMinimumNetworks) ? "ip" : "wifi";
	_position.time = real_time_clock_usecs();
	return B_OK;
}


/*!	~/config/settings/system/location (driver settings):
		enabled false			no positions at all
		service <URL>			a geolocation service with Mozilla Location
								Service's protocol (default BeaconDB)
		fixed <latitude> <longitude> [<accuracy in meters>]
								this position, nothing asked
*/
void
LocationServer::_LoadSettings()
{
	fEnabled = true;
	fService = kDefaultService;
	fHasFixed = false;

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append("system/location") != B_OK) {
		return;
	}
	void* settings = load_driver_settings(path.Path());
	if (settings == NULL)
		return;

	fEnabled = get_driver_boolean_parameter(settings, "enabled", true, true);
	const char* service = get_driver_parameter(settings, "service", NULL,
		NULL);
	if (service != NULL)
		fService = service;

	const driver_settings* parsed = get_driver_settings(settings);
	for (int32 i = 0; parsed != NULL && i < parsed->parameter_count; i++) {
		const driver_parameter& parameter = parsed->parameters[i];
		if (strcmp(parameter.name, "fixed") != 0 || parameter.value_count < 2)
			continue;
		fFixed.latitude = strtod(parameter.values[0], NULL);
		fFixed.longitude = strtod(parameter.values[1], NULL);
		fFixed.accuracy = parameter.value_count > 2
			? strtod(parameter.values[2], NULL) : 10;
		fFixed.source = "fixed";
		fHasFixed = fFixed.latitude >= -90 && fFixed.latitude <= 90
			&& fFixed.longitude >= -180 && fFixed.longitude <= 180;
	}
	unload_driver_settings(settings);
}


int
main()
{
	status_t status;
	LocationServer server(&status);
	if (status != B_OK)
		return 1;
	server.Run();
	return 0;
}
