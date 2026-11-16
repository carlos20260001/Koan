/*
 * Copyright 2007-2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MOUNT_SERVER_H
#define _MOUNT_SERVER_H

#include <SupportDefs.h>


const uint32 kMountVolume 			= 'mntv';
const uint32 kMountAllNow			= 'mntn';
const uint32 kSetAutomounterParams 	= 'pmst';
const uint32 kGetAutomounterParams 	= 'gpms';
const uint32 kVolumeMounted			= 'vmtd';
const uint32 kUnmountVolume			= 'umnt';

// Network shares, which the mount server keeps a list of and mounts when the
// system starts. The password of a share is in the key store, and never in a
// reply.
//
// A share is a message with these fields:
//	"id"				int32	what the others below tell the share by
//	"name"				string	what the volume is called
//	"server"			string	host name or address, ":port" if not 445
//	"share"				string
//	"path"				string	the directory of the share to mount, if not
//								all of it
//	"user"				string	a guest if empty
//	"domain"			string
//	"mount at startup"	bool
//	"read only"			bool
//	"encrypt"			bool	SMB3 only
// and, in replies:
//	"has password"		bool
//	"mounted"			bool
//	"device"			int32	if mounted
//	"mount point"		string	if mounted
//	"error"				int32	what the last try to mount it ended with
//
// kGetNetworkShares is answered with all shares, as "share" messages.
// kSetNetworkShare takes the fields of a share: with an "id" that share is
// changed, without one it is added. A "password" (string) replaces the one
// that was stored, none leaves it. With "mount" (bool) the share is mounted
// then; one that is mounted and no longer is what it was mounted as is
// mounted anew in any case.
// The other three take an "id".
//
// All are answered with a kNetworkShareReply that has the "request" (int32)
// it answers and, but for kGetNetworkShares, the "id" and "name" of the share
// and an "error" (int32). The answer comes when all is done, which for
// mounting is when the server has answered or was given up on.
const uint32 kGetNetworkShares		= 'gnsh';
const uint32 kSetNetworkShare		= 'snsh';
const uint32 kRemoveNetworkShare	= 'rnsh';
const uint32 kMountNetworkShare		= 'mnsh';
const uint32 kUnmountNetworkShare	= 'unsh';
const uint32 kNetworkShareReply		= 'nsrp';

#define kMountServerSignature "application/x-vnd.vos-mount_server"


#endif // _MOUNT_SERVER_H
