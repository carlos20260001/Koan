/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef LINUX_WIRELESS_H
#define LINUX_WIRELESS_H


#include <SupportDefs.h>


/*!	What the Wi-Fi panels asked Haiku's net80211 stack through SIOCG80211,
	read from Linux's wireless extensions (cfg80211's WEXT compatibility,
	which any user may query): the SSID, the channel frequency and the
	transmit rate of the current association. Kept in a file of its own:
	<linux/wireless.h> cannot share a translation unit with Haiku's
	<net/if.h>.
*/

// Copies the SSID (not NUL-terminated, up to 32 bytes) and returns its
// length, or a negative error.
ssize_t		linux_wireless_get_ssid(const char* interface, char* ssid,
				size_t size);
// The frequency of the channel in MHz, or 0.
int32		linux_wireless_get_frequency(const char* interface);
// The last transmit rate in kbit/s, or 0.
uint32		linux_wireless_get_tx_rate(const char* interface);


#endif	// LINUX_WIRELESS_H
