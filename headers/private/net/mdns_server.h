/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MDNS_SERVER_H
#define _MDNS_SERVER_H

/*!	The messages mdns_server understands; see docs/airos/MDNS.md. */

#define MDNS_SERVER_SIGNATURE	"application/x-vnd.vos-mdns_server"

enum {
	MDNS_REGISTER_NAME		= 'mdrg',
		// "name" (string, ending in ".local"), "address" (string, IPv4,
		// optional). Reply: "status" (int32). The name is answered for until
		// MDNS_UNREGISTER_NAME, or until the sender's team is gone.
	MDNS_UNREGISTER_NAME	= 'mdur'
		// "name" (string). Reply: "status" (int32).
};

#endif	// _MDNS_SERVER_H
