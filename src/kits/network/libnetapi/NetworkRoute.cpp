/*
 * Copyright 2013-2015 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <NetworkRoute.h>

#include <errno.h>
#include <new>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AutoDeleter.h>


// Linux's sockaddr has no sa_len: the length follows from the family.
static size_t
sockaddr_length(const sockaddr& address)
{
	switch (address.sa_family) {
		case AF_INET:
			return sizeof(sockaddr_in);
		case AF_INET6:
			return sizeof(sockaddr_in6);
		default:
			return sizeof(sockaddr_storage);
	}
}


BNetworkRoute::BNetworkRoute()
{
	memset(&fRouteEntry, 0, sizeof(route_entry));
}


BNetworkRoute::~BNetworkRoute()
{
	UnsetDestination();
	UnsetMask();
	UnsetGateway();
	UnsetSource();
}


status_t
BNetworkRoute::SetTo(const BNetworkRoute& other)
{
	return SetTo(other.RouteEntry());
}


status_t
BNetworkRoute::SetTo(const route_entry& routeEntry)
{
	#define SET_ADDRESS(address, setFunction) \
		if (routeEntry.address != NULL) { \
			result = setFunction(*routeEntry.address); \
			if (result != B_OK) \
				return result; \
		}

	status_t result;
	SET_ADDRESS(destination, SetDestination)
	SET_ADDRESS(mask, SetMask)
	SET_ADDRESS(gateway, SetGateway)
	SET_ADDRESS(source, SetSource)

	SetFlags(routeEntry.flags);
	SetMTU(routeEntry.mtu);
	return B_OK;
}


void
BNetworkRoute::Adopt(BNetworkRoute& other)
{
	memcpy(&fRouteEntry, &other.fRouteEntry, sizeof(route_entry));
	memset(&other.fRouteEntry, 0, sizeof(route_entry));
}


const route_entry&
BNetworkRoute::RouteEntry() const
{
	return fRouteEntry;
}


const sockaddr*
BNetworkRoute::Destination() const
{
	return fRouteEntry.destination;
}


status_t
BNetworkRoute::SetDestination(const sockaddr& destination)
{
	return _AllocateAndSetAddress(destination, fRouteEntry.destination);
}


void
BNetworkRoute::UnsetDestination()
{
	_FreeAndUnsetAddress(fRouteEntry.destination);
}


const sockaddr*
BNetworkRoute::Mask() const
{
	return fRouteEntry.mask;
}


status_t
BNetworkRoute::SetMask(const sockaddr& mask)
{
	return _AllocateAndSetAddress(mask, fRouteEntry.mask);
}


void
BNetworkRoute::UnsetMask()
{
	_FreeAndUnsetAddress(fRouteEntry.mask);
}


const sockaddr*
BNetworkRoute::Gateway() const
{
	return fRouteEntry.gateway;
}


status_t
BNetworkRoute::SetGateway(const sockaddr& gateway)
{
	return _AllocateAndSetAddress(gateway, fRouteEntry.gateway);
}


void
BNetworkRoute::UnsetGateway()
{
	_FreeAndUnsetAddress(fRouteEntry.gateway);
}


const sockaddr*
BNetworkRoute::Source() const
{
	return fRouteEntry.source;
}


status_t
BNetworkRoute::SetSource(const sockaddr& source)
{
	return _AllocateAndSetAddress(source, fRouteEntry.source);
}


void
BNetworkRoute::UnsetSource()
{
	_FreeAndUnsetAddress(fRouteEntry.source);
}


uint32
BNetworkRoute::Flags() const
{
	return fRouteEntry.flags;
}


void
BNetworkRoute::SetFlags(uint32 flags)
{
	fRouteEntry.flags = flags;
}


uint32
BNetworkRoute::MTU() const
{
	return fRouteEntry.mtu;
}


void
BNetworkRoute::SetMTU(uint32 mtu)
{
	fRouteEntry.mtu = mtu;
}


int
BNetworkRoute::AddressFamily() const
{
	#define RETURN_FAMILY_IF_SET(address) \
		if (fRouteEntry.address != NULL \
			&& fRouteEntry.address->sa_family != AF_UNSPEC) { \
			return fRouteEntry.address->sa_family; \
		}

	RETURN_FAMILY_IF_SET(destination)
	RETURN_FAMILY_IF_SET(mask)
	RETURN_FAMILY_IF_SET(gateway)
	RETURN_FAMILY_IF_SET(source)

	return AF_UNSPEC;
}


status_t
BNetworkRoute::GetDefaultRoute(int family, const char* interfaceName,
	BNetworkRoute& route)
{
	BObjectList<BNetworkRoute, true> routes(1);
	status_t result = GetRoutes(family, interfaceName, RTF_DEFAULT, routes);
	if (result != B_OK)
		return result;

	if (routes.CountItems() == 0)
		return B_ENTRY_NOT_FOUND;

	route.Adopt(*routes.ItemAt(0));
	return B_OK;
}


status_t
BNetworkRoute::GetDefaultGateway(int family, const char* interfaceName,
	sockaddr& gateway)
{
	BNetworkRoute route;
	status_t result = GetDefaultRoute(family, interfaceName, route);
	if (result != B_OK)
		return result;

	const sockaddr* defaultGateway = route.Gateway();
	if (defaultGateway == NULL)
		return B_ENTRY_NOT_FOUND;

	memcpy(&gateway, defaultGateway, sockaddr_length(*defaultGateway));
	return B_OK;
}


status_t
BNetworkRoute::GetRoutes(int family, BObjectList<BNetworkRoute, true>& routes)
{
	return GetRoutes(family, NULL, 0, routes);
}


status_t
BNetworkRoute::GetRoutes(int family, const char* interfaceName,
	BObjectList<BNetworkRoute, true>& routes)
{
	return GetRoutes(family, interfaceName, 0, routes);
}


// Linux flags in /proc/net/route and /proc/net/ipv6_route (RTF_* of
// <linux/route.h>, which cannot be included next to Haiku's <net/route.h>).
static const uint32 kLinuxRouteUp = 0x0001;
static const uint32 kLinuxRouteGateway = 0x0002;
static const uint32 kLinuxRouteHost = 0x0004;
static const uint32 kLinuxRouteDynamic = 0x0010;


static uint32
haiku_route_flags(uint32 linuxFlags, bool isDefault)
{
	uint32 flags = 0;
	if ((linuxFlags & kLinuxRouteUp) != 0)
		flags |= RTF_UP;
	if ((linuxFlags & kLinuxRouteGateway) != 0)
		flags |= RTF_GATEWAY;
	if ((linuxFlags & kLinuxRouteHost) != 0)
		flags |= RTF_HOST;
	if ((linuxFlags & kLinuxRouteDynamic) != 0)
		flags |= RTF_DYNAMIC;
	else
		flags |= RTF_STATIC;
	if (isDefault)
		flags |= RTF_DEFAULT;
	return flags;
}


static status_t
add_route(BObjectList<BNetworkRoute, true>& routes, uint32 filterFlags,
	route_entry& entry)
{
	if (filterFlags != 0 && (entry.flags & filterFlags) == 0)
		return B_OK;

	BNetworkRoute* route = new(std::nothrow) BNetworkRoute;
	if (route == NULL)
		return B_NO_MEMORY;

	status_t result = route->SetTo(entry);
	if (result != B_OK) {
		delete route;
		return result;
	}

	if (!routes.AddItem(route)) {
		delete route;
		return B_NO_MEMORY;
	}

	return B_OK;
}


static status_t
get_inet_routes(const char* interfaceName, uint32 filterFlags,
	BObjectList<BNetworkRoute, true>& routes)
{
	FILE* file = fopen("/proc/net/route", "r");
	if (file == NULL)
		return errno;

	char line[512];
	// The first line names the columns.
	if (fgets(line, sizeof(line), file) == NULL) {
		fclose(file);
		return B_OK;
	}

	status_t result = B_OK;
	while (result == B_OK && fgets(line, sizeof(line), file) != NULL) {
		char name[IF_NAMESIZE + 1];
		unsigned int destination, gateway, flags, refCount, use, metric, mask,
			mtu;
		if (sscanf(line, "%16s %x %x %x %u %u %u %x %u", name, &destination,
				&gateway, &flags, &refCount, &use, &metric, &mask, &mtu)
				!= 9) {
			continue;
		}
		if (interfaceName != NULL && strcmp(name, interfaceName) != 0)
			continue;

		// The addresses are in network byte order already.
		sockaddr_in destinationAddress = {};
		destinationAddress.sin_family = AF_INET;
		destinationAddress.sin_addr.s_addr = destination;
		sockaddr_in maskAddress = {};
		maskAddress.sin_family = AF_INET;
		maskAddress.sin_addr.s_addr = mask;
		sockaddr_in gatewayAddress = {};
		gatewayAddress.sin_family = AF_INET;
		gatewayAddress.sin_addr.s_addr = gateway;

		route_entry entry = {};
		entry.destination = (sockaddr*)&destinationAddress;
		entry.mask = (sockaddr*)&maskAddress;
		if ((flags & kLinuxRouteGateway) != 0)
			entry.gateway = (sockaddr*)&gatewayAddress;
		entry.flags = haiku_route_flags(flags, destination == 0 && mask == 0);
		entry.mtu = mtu;

		result = add_route(routes, filterFlags, entry);
	}

	fclose(file);
	return result;
}


static bool
parse_inet6_address(const char* hex, in6_addr& address)
{
	if (strlen(hex) != 32)
		return false;

	for (int i = 0; i < 16; i++) {
		unsigned int byte;
		if (sscanf(hex + 2 * i, "%2x", &byte) != 1)
			return false;
		address.s6_addr[i] = (uint8)byte;
	}
	return true;
}


static status_t
get_inet6_routes(const char* interfaceName, uint32 filterFlags,
	BObjectList<BNetworkRoute, true>& routes)
{
	FILE* file = fopen("/proc/net/ipv6_route", "r");
	if (file == NULL)
		return errno == ENOENT ? B_OK : errno;

	char line[512];
	status_t result = B_OK;
	while (result == B_OK && fgets(line, sizeof(line), file) != NULL) {
		char destinationHex[33], sourceHex[33], gatewayHex[33];
		char name[IF_NAMESIZE + 1];
		unsigned int destinationPrefix, sourcePrefix, metric, refCount, use,
			flags;
		if (sscanf(line, "%32s %x %32s %x %32s %x %x %x %x %16s",
				destinationHex, &destinationPrefix, sourceHex, &sourcePrefix,
				gatewayHex, &metric, &refCount, &use, &flags, name) != 10) {
			continue;
		}
		if (interfaceName != NULL && strcmp(name, interfaceName) != 0)
			continue;

		sockaddr_in6 destinationAddress = {};
		destinationAddress.sin6_family = AF_INET6;
		sockaddr_in6 gatewayAddress = {};
		gatewayAddress.sin6_family = AF_INET6;
		if (!parse_inet6_address(destinationHex,
				destinationAddress.sin6_addr)
			|| !parse_inet6_address(gatewayHex, gatewayAddress.sin6_addr)) {
			continue;
		}

		sockaddr_in6 maskAddress = {};
		maskAddress.sin6_family = AF_INET6;
		for (unsigned int bit = 0; bit < destinationPrefix && bit < 128; bit++)
			maskAddress.sin6_addr.s6_addr[bit / 8] |= 0x80 >> (bit % 8);

		route_entry entry = {};
		entry.destination = (sockaddr*)&destinationAddress;
		entry.mask = (sockaddr*)&maskAddress;
		if ((flags & kLinuxRouteGateway) != 0)
			entry.gateway = (sockaddr*)&gatewayAddress;
		entry.flags = haiku_route_flags(flags, destinationPrefix == 0);

		result = add_route(routes, filterFlags, entry);
	}

	fclose(file);
	return result;
}


status_t
BNetworkRoute::GetRoutes(int family, const char* interfaceName,
	uint32 filterFlags, BObjectList<BNetworkRoute, true>& routes)
{
	// Haiku's stack answers SIOCGRTTABLE; Linux shows its main routing
	// tables in /proc (netlink would also give the other tables, which
	// this API has no way to name).
	switch (family) {
		case AF_INET:
			return get_inet_routes(interfaceName, filterFlags, routes);
		case AF_INET6:
			return get_inet6_routes(interfaceName, filterFlags, routes);
		default:
			return B_NOT_SUPPORTED;
	}
}


status_t
BNetworkRoute::_AllocateAndSetAddress(const sockaddr& from,
	sockaddr*& to)
{
	if (to == NULL) {
		to = (sockaddr*)malloc(sizeof(sockaddr_storage));
		if (to == NULL)
			return B_NO_MEMORY;
	}

	memcpy(to, &from, sockaddr_length(from));
	return B_OK;
}


void
BNetworkRoute::_FreeAndUnsetAddress(sockaddr*& address)
{
	free(address);
	address = NULL;
}
