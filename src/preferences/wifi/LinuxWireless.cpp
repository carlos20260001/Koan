/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LinuxWireless.h"

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/wireless.h>


static int
wireless_request(const char* interface, unsigned long request,
	struct iwreq& data)
{
	if (interface == NULL)
		return -EINVAL;

	int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;

	strncpy(data.ifr_name, interface, IFNAMSIZ - 1);
	data.ifr_name[IFNAMSIZ - 1] = '\0';
	int result = ioctl(fd, request, &data) < 0 ? -errno : 0;
	close(fd);
	return result;
}


ssize_t
linux_wireless_get_ssid(const char* interface, char* ssid, size_t size)
{
	char buffer[IW_ESSID_MAX_SIZE + 1];
	memset(buffer, 0, sizeof(buffer));

	struct iwreq data;
	memset(&data, 0, sizeof(data));
	data.u.essid.pointer = buffer;
	data.u.essid.length = sizeof(buffer);
	int result = wireless_request(interface, SIOCGIWESSID, data);
	if (result < 0)
		return result;

	size_t length = data.u.essid.length;
	if (length > IW_ESSID_MAX_SIZE)
		length = IW_ESSID_MAX_SIZE;
	if (length > size)
		length = size;
	memcpy(ssid, buffer, length);
	return (ssize_t)length;
}


int32
linux_wireless_get_frequency(const char* interface)
{
	struct iwreq data;
	memset(&data, 0, sizeof(data));
	if (wireless_request(interface, SIOCGIWFREQ, data) < 0)
		return 0;

	// m * 10^e Hz; small values without an exponent are channel numbers
	double frequency = data.u.freq.m;
	for (int i = 0; i < data.u.freq.e; i++)
		frequency *= 10;
	if (frequency < 1000)
		return 0;
	return (int32)(frequency / 1000000);
}


uint32
linux_wireless_get_tx_rate(const char* interface)
{
	struct iwreq data;
	memset(&data, 0, sizeof(data));
	if (wireless_request(interface, SIOCGIWRATE, data) < 0)
		return 0;
	if (data.u.bitrate.value <= 0)
		return 0;
	return (uint32)(data.u.bitrate.value / 1000);
}
