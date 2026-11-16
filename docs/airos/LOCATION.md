# Location service

`location_server` (on demand, `application/x-vnd.vos-location_server`)
tells programs where the computer is, so that they don't each look it up
themselves. Messages are in `<location_server.h>` (private headers, `net`).

## Asking

Send `LOCATION_GET_POSITION` (`'lcgp'`) and wait for the reply:

| field | type | |
| --- | --- | --- |
| `max_age` (request, optional) | int64 | microseconds; a position determined this long ago at most will do (default 60 s, 0 asks again) |
| `status` | int32 | `B_OK`, or `B_NOT_ALLOWED` when location is switched off, or the lookup's error |
| `latitude`, `longitude` | double | degrees, WGS 84 |
| `accuracy` | double | meters, the radius |
| `source` | string | `fixed`, `wifi` or `ip` |
| `time` | int64 | `real_time_clock_usecs()` when it was determined |

```cpp
BMessenger server(LOCATION_SERVER_SIGNATURE);
BMessage request(LOCATION_GET_POSITION), reply;
if (server.SendMessage(&request, &reply) == B_OK
	&& reply.GetInt32("status", B_ERROR) == B_OK) {
	double latitude = reply.GetDouble("latitude", 0);
	...
}
```

A lookup takes about a second; answers from the cache come at once.
Requests that arrive during a lookup get its result.

## Where the position comes from

1. A position the user entered (`fixed` below): exact, nothing is sent
   anywhere. The way to get a precise position on a desktop.
2. A geolocation service with Mozilla Location Service's protocol,
   [BeaconDB](https://beacondb.net) by default: the hardware addresses of
   the Wi-Fi networks in range (from the last scan; locally administered,
   random addresses and networks named `*_nomap` are left out). With fewer
   than two networks, or when the service does not know them, it answers
   from the network address (`source` `ip`, about 25 km).

There is no GPS source yet.

## Settings

`~/config/settings/system/location` (driver settings), read for every
request:

```
enabled false                 # no positions at all (B_NOT_ALLOWED)
service https://...           # another geolocation service
fixed -41.4332 147.1441 30    # latitude, longitude, accuracy in meters
```

Programs still ask their users before they use a position (Summit's site
permissions); this switch is the system-wide one.

## Checking it

`location_test [max age in seconds]` (`src/tests/servers/location`) asks
and prints the answer. On the X399 (wired, the Wi-Fi card scanning):
BeaconDB answered in 0.8 s from the network address (25 km; it did not know
the networks in range), the cached answer in 0 ms; `fixed` gave the entered
position, `enabled false` `B_NOT_ALLOWED`.
