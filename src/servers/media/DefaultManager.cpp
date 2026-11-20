/*
 * Copyright 2002, 2003 Marcus Overhagen, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "DefaultManager.h"

#include <PipeWireNodes.h>

#include <Application.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <MediaNode.h>
#include <OS.h>
#include <Path.h>
#include <TimeSource.h>
#include <new>
#include <string.h>

#include "MediaDebug.h"
#include "DormantNodeManager.h"
#include "media_server.h"
#include "NodeManager.h"


/* no locking used in this file, we assume that the caller (NodeManager) does it.
 */


#define MAX_NODE_INFOS 10
#define MAX_INPUT_INFOS 10

const uint32 kMsgHeader = 'sepx';
const uint32 kMsgTypeVideoIn = 0xffffffef;
const uint32 kMsgTypeVideoOut = 0xffffffee;
const uint32 kMsgTypeAudioIn = 0xfffffffe;
const uint32 kMsgTypeAudioOut = 0xffffffff;

const char *kDefaultManagerType 			= "be:_default";
const char *kDefaultManagerAddon 			= "be:_addon_id";
const char *kDefaultManagerFlavorId 		= "be:_internal_id";
const char *kDefaultManagerFlavorName 		= "be:_flavor_name";
const char *kDefaultManagerPath 			= "be:_path";
const char *kDefaultManagerInput 			= "be:_input_id";

const char *kDefaultManagerSettingsDirectory			= "Media";
const char *kDefaultManagerSettingsFile				= "MDefaultManager";


DefaultManager::DefaultManager()
	:
	fMixerConnected(false),
 	fPhysicalVideoOut(-1),
	fPhysicalVideoIn(-1),
	fPhysicalAudioOut(-1),
	fPhysicalAudioIn(-1),
	fSystemTimeSource(-1),
	fTimeSource(-1),
	fAudioMixer(-1),
	fPhysicalAudioOutInputID(0),
	fVideoOutStandIn(false),
	fVideoInStandIn(false),
	fAudioOutStandIn(false),
	fAudioInStandIn(false),
	fSettingsLock("default manager settings"),
	fRescanThread(-1),
	fRescanRequested(0),
	fRescanLock("rescan default manager"),
	fRoster(NULL)
{
	strcpy(fPhysicalAudioOutInputName, "default");
	fBeginHeader[0] = 0xab00150b;
	fBeginHeader[1] = 0x18723462;
	fBeginHeader[2] = 0x00000002;
	fEndHeader[0] = 0x7465726d;
	fEndHeader[1] = 0x6d666c67;
	fEndHeader[2] = 0x00000002;

	fRoster = BMediaRoster::Roster();
	if (fRoster == NULL)
		TRACE("DefaultManager: The roster is NULL\n");
}


DefaultManager::~DefaultManager()
{
	for (int32 i = 0; i < fMsgList.CountItems(); i++)
		delete (BMessage *)fMsgList.ItemAt(i);
}


// this is called by the media_server *before* any add-ons have been loaded
status_t
DefaultManager::LoadState()
{
	CALLED();
	status_t err = B_OK;
	BPath path;
	if ((err = find_directory(B_USER_SETTINGS_DIRECTORY, &path)) != B_OK)
		return err;

	path.Append(kDefaultManagerSettingsDirectory);
	path.Append(kDefaultManagerSettingsFile);

	BFile file(path.Path(), B_READ_ONLY);

	uint32 categoryCount;
	ssize_t size = sizeof(uint32) * 3;
	if (file.Read(fBeginHeader, size) < size)
		return B_ERROR;
	TRACE("0x%08lx %ld\n", fBeginHeader[0], fBeginHeader[0]);
	TRACE("0x%08lx %ld\n", fBeginHeader[1], fBeginHeader[1]);
	TRACE("0x%08lx %ld\n", fBeginHeader[2], fBeginHeader[2]);
	size = sizeof(uint32);
	if (file.Read(&categoryCount, size) < size) {
		fprintf(stderr,
			"DefaultManager::LoadState() failed to read categoryCount\n");
		return B_ERROR;
	}
	TRACE("DefaultManager::LoadState() categoryCount %ld\n", categoryCount);
	while (categoryCount--) {
		BMessage settings;
		uint32 msg_header;
		uint32 default_type;
		if (file.Read(&msg_header, size) < size) {
			fprintf(stderr,
				"DefaultManager::LoadState() failed to read msg_header\n");
			return B_ERROR;
		}
		if (file.Read(&default_type, size) < size) {
			fprintf(stderr,
				"DefaultManager::LoadState() failed to read default_type\n");
			return B_ERROR;
		}
		if (settings.Unflatten(&file) == B_OK)
			fMsgList.AddItem(new BMessage(settings));
		else
			fprintf(stderr, "DefaultManager::LoadState() failed to unflatten\n");
	}
	size = sizeof(uint32) * 3;
	if (file.Read(fEndHeader,size) < size) {
		fprintf(stderr,
			"DefaultManager::LoadState() failed to read fEndHeader\n");
		return B_ERROR;
	}

	TRACE("LoadState returns B_OK\n");
	return B_OK;
}


/*!	Writes the defaults the user chose. Not the ones in use: those may only
	stand in for a chosen device that is not there (a USB DAC that is
	unplugged, or found only later during start-up), and saving them would
	lose the choice.
*/
status_t
DefaultManager::SaveState()
{
	CALLED();
	status_t err = B_OK;
	BPath path;
	if ((err = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true)) != B_OK)
		return err;
	path.Append(kDefaultManagerSettingsDirectory);
	if ((err = create_directory(path.Path(), 0755)) != B_OK)
		return err;
	path.Append(kDefaultManagerSettingsFile);

	BAutolock locker(fSettingsLock);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);

	if (file.Write(fBeginHeader, sizeof(uint32)*3) < (int32)sizeof(uint32)*3)
		return B_ERROR;
	int32 categoryCount = fMsgList.CountItems();
	if (file.Write(&categoryCount, sizeof(uint32)) < (int32)sizeof(uint32))
		return B_ERROR;

	for (int32 i = 0; i < categoryCount; i++) {
		BMessage *settings = (BMessage *)fMsgList.ItemAt(i);
		uint32 default_type;
		if (settings->FindInt32(kDefaultManagerType,
			(int32*)&default_type) < B_OK)
			return B_ERROR;
		if (file.Write(&kMsgHeader, sizeof(uint32)) < (int32)sizeof(uint32))
			return B_ERROR;
		if (file.Write(&default_type, sizeof(uint32)) < (int32)sizeof(uint32))
			return B_ERROR;
		if (settings->Flatten(&file) < B_OK)
			return B_ERROR;
	}
	if (file.Write(fEndHeader, sizeof(uint32)*3) < (int32)sizeof(uint32)*3)
		return B_ERROR;

	return B_OK;
}


/*!	The current default of \a type was chosen by the user: it is the one to
	save, and to look for again after a restart or when the device comes
	back. A node that is not an add-on's flavor cannot be found again; then
	nothing is saved for the type.
*/
status_t
DefaultManager::RememberDefault(NodeManager *node_manager, node_type type)
{
	CALLED();
	uint32 default_type;
	media_node_id node_id;
	switch (type) {
		case VIDEO_INPUT:
			default_type = kMsgTypeVideoIn;
			node_id = fPhysicalVideoIn;
			break;
		case VIDEO_OUTPUT:
			default_type = kMsgTypeVideoOut;
			node_id = fPhysicalVideoOut;
			break;
		case AUDIO_INPUT:
			default_type = kMsgTypeAudioIn;
			node_id = fPhysicalAudioIn;
			break;
		case AUDIO_OUTPUT:
			default_type = kMsgTypeAudioOut;
			node_id = fPhysicalAudioOut;
			break;
		default:
			return B_BAD_VALUE;
	}

	// we call the node manager to have more infos about nodes
	BMessage *settings = NULL;
	dormant_node_info info;
	media_node node;
	entry_ref ref;
	if (node_id != -1 && node_manager->GetCloneForID(node_id, be_app->Team(),
			&node) == B_OK) {
		status_t status = node_manager->GetDormantNodeInfo(node, &info);
		node_manager->ReleaseNodeReference(node_id, be_app->Team());
		if (status == B_OK
			&& node_manager->GetAddOnRef(info.addon, &ref) == B_OK) {
			settings = new(std::nothrow) BMessage();
		}
	}
	if (settings != NULL) {
		BPath path(&ref);
		settings->AddInt32(kDefaultManagerType, default_type);
		settings->AddInt32(kDefaultManagerAddon, info.addon);
		settings->AddInt32(kDefaultManagerFlavorId, info.flavor_id);
		settings->AddInt32(kDefaultManagerInput,
			default_type == kMsgTypeAudioOut ? fPhysicalAudioOutInputID : 0);
		settings->AddString(kDefaultManagerFlavorName, info.name);
		settings->AddString(kDefaultManagerPath, path.Path());
		TRACE("message %s added\n", info.name);
	}

	BAutolock locker(fSettingsLock);
	for (int32 i = fMsgList.CountItems() - 1; i >= 0; i--) {
		BMessage *saved = (BMessage *)fMsgList.ItemAt(i);
		int32 msgType;
		if (saved->FindInt32(kDefaultManagerType, &msgType) == B_OK
			&& (uint32)msgType == default_type) {
			fMsgList.RemoveItem(i);
			delete saved;
		}
	}
	if (settings != NULL && !fMsgList.AddItem(settings)) {
		delete settings;
		return B_NO_MEMORY;
	}
	return B_OK;
}


status_t
DefaultManager::Set(media_node_id node_id, const char *input_name,
	int32 input_id, node_type type)
{
	CALLED();
	TRACE("DefaultManager::Set type : %i, node : %li, input : %li\n", type,
		node_id, input_id);
	switch (type) {
		case VIDEO_INPUT:
			fPhysicalVideoIn = node_id;
			fVideoInStandIn = false;
			return B_OK;
		case AUDIO_INPUT:
			fPhysicalAudioIn = node_id;
			fAudioInStandIn = false;
			return B_OK;
		case VIDEO_OUTPUT:
			fPhysicalVideoOut = node_id;
			fVideoOutStandIn = false;
			return B_OK;
		case AUDIO_MIXER:
			return B_ERROR;
		case AUDIO_OUTPUT:
			fAudioOutStandIn = false;
			if (fPhysicalAudioOut == node_id && fPhysicalAudioOutInputID == input_id)
				return B_OK;

			fPhysicalAudioOut = node_id;
			fPhysicalAudioOutInputID = input_id;
			strcpy(fPhysicalAudioOutInputName,
				input_name ? input_name : "<null>");

			fMixerConnected = false;
			Rescan();
			return B_OK;
		case TIME_SOURCE:
			return B_ERROR;

		// called by the media_server's ServerApp::StartSystemTimeSource()
		case SYSTEM_TIME_SOURCE:
		{
			ASSERT(fSystemTimeSource == -1);
			fSystemTimeSource = node_id;
			return B_OK;
		}

		default:
		{
			ERROR("DefaultManager::Set Error: called with unknown type %d\n",
				type);
			return B_ERROR;
		}
	}
}


status_t
DefaultManager::Get(media_node_id *nodeid, char *input_name, int32 *inputid,
	node_type type)
{
	CALLED();
	switch (type) {
		case VIDEO_INPUT: 		// output: nodeid
			if (fPhysicalVideoIn == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fPhysicalVideoIn;
			return B_OK;

		case AUDIO_INPUT: 		// output: nodeid
			if (fPhysicalAudioIn == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fPhysicalAudioIn;
			return B_OK;

		case VIDEO_OUTPUT: 		// output: nodeid
			if (fPhysicalVideoOut == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fPhysicalVideoOut;
			return B_OK;

		case AUDIO_OUTPUT:		// output: nodeid
			if (fPhysicalAudioOut == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fPhysicalAudioOut;
			return B_OK;

		case AUDIO_OUTPUT_EX:	// output: nodeid, input_name, input_id
			if (fPhysicalAudioOut == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fPhysicalAudioOut;
			*inputid = fPhysicalAudioOutInputID;
			strcpy(input_name, fPhysicalAudioOutInputName);
			return B_OK;

		case AUDIO_MIXER:		// output: nodeid
			if (fAudioMixer == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fAudioMixer;
			return B_OK;

		case TIME_SOURCE:
			if (fTimeSource != -1)
				*nodeid = fTimeSource;
			else
				*nodeid = fSystemTimeSource;
			return B_OK;

		case SYSTEM_TIME_SOURCE:
			*nodeid = fSystemTimeSource;
			return B_OK;

		default:
		{
			ERROR("DefaultManager::Get Error: called with unknown type %d\n",
				type);
			return B_ERROR;
		}
	}
}


// this is called by the media_server *after* the initial add-on loading
// has been done
status_t
DefaultManager::Rescan()
{
	BAutolock locker(fRescanLock);
	atomic_add(&fRescanRequested, 1);
	if (fRescanThread < 0) {
		fRescanThread = spawn_thread(rescan_thread, "rescan defaults",
			B_NORMAL_PRIORITY - 2, this);
		resume_thread(fRescanThread);
	}

	return B_OK;
}


int32
DefaultManager::rescan_thread(void *arg)
{
	reinterpret_cast<DefaultManager *>(arg)->_RescanThread();
	return 0;
}


void
DefaultManager::_RescanThread()
{
	TRACE("DefaultManager::_RescanThread() enter\n");

	BAutolock locker(fRescanLock);

	while (atomic_and(&fRescanRequested, 0) != 0) {
		locker.Unlock();

		// We do not search for the system time source,
		// it should already exist
		ASSERT(fSystemTimeSource != -1);

		_ChoosePhysical(&fPhysicalVideoOut, &fVideoOutStandIn,
			kMsgTypeVideoOut, false, true);
		_ChoosePhysical(&fPhysicalVideoIn, &fVideoInStandIn,
			kMsgTypeVideoIn, true, true);
		media_node_id audioOut = fPhysicalAudioOut;
		_ChoosePhysical(&fPhysicalAudioOut, &fAudioOutStandIn,
			kMsgTypeAudioOut, false, false);
		if (audioOut != -1 && fPhysicalAudioOut != audioOut) {
			// the saved output is back: the mixer moves over to it
			fMixerConnected = false;
		}
		_ChoosePhysical(&fPhysicalAudioIn, &fAudioInStandIn,
			kMsgTypeAudioIn, true, false);
		if (fAudioMixer == -1)
			_FindAudioMixer();

		// The normal time source is searched for after the
		// Physical Audio Out has been created.
		if (fTimeSource == -1)
			_FindTimeSource();

		// Connect the mixer and physical audio out (soundcard)
		if (!fMixerConnected && fAudioMixer != -1 && fPhysicalAudioOut != -1) {
			fMixerConnected = _ConnectMixerToOutput() == B_OK;
			if (!fMixerConnected)
				TRACE("DefaultManager: failed to connect mixer and "
					"soundcard\n");
		} else {
			TRACE("DefaultManager: Did not try to connect mixer and "
				"soundcard\n");
		}

		if (fMixerConnected) {
			add_on_server_rescan_finished_notify_command cmd;
			SendToAddOnServer(ADD_ON_SERVER_RESCAN_FINISHED_NOTIFY, &cmd,
				sizeof(cmd));
		}

		locker.Lock();
	}

	fRescanThread = -1;

	BMessage msg(MEDIA_SERVER_RESCAN_COMPLETED);
	be_app->PostMessage(&msg);

	TRACE("DefaultManager::_RescanThread() leave\n");
}


/*!	Chooses a default when there is none, and goes back to the saved one
	when it turns up while another device stands in for it: a USB DAC or
	camera that is plugged in again, or that is found only after the first
	rescan at start-up.
*/
void
DefaultManager::_ChoosePhysical(volatile media_node_id *id,
	volatile bool *standIn, uint32 default_type, bool isInput, bool isVideo)
{
	if (*id != -1 && !*standIn)
		return;

	bool savedOnly = *id != -1;
	media_node_id chosen = *id;
	bool saved = _FindPhysical(&chosen, default_type, isInput,
		isVideo ? B_MEDIA_RAW_VIDEO : B_MEDIA_RAW_AUDIO, savedOnly);
	if (isVideo && !saved) {
		saved = _FindPhysical(&chosen, default_type, isInput,
			B_MEDIA_ENCODED_VIDEO, savedOnly);
	}

	// (the user may have chosen another device in the meantime)
	if (savedOnly && !*standIn)
		return;

	*id = chosen;
	if (saved)
		*standIn = false;
	else if (!savedOnly)
		*standIn = true;
}


/*!	Looks for the saved default among the live nodes; unless \a savedOnly
	is set, another node is taken when that one is not there. Returns
	whether the saved default was taken.
*/
bool
DefaultManager::_FindPhysical(volatile media_node_id *id, uint32 default_type,
	bool isInput, media_type type, bool savedOnly)
{
	live_node_info info[MAX_NODE_INFOS];
	media_format format;
	int32 count;
	status_t rv;
	bool haveSaved = false;
	BPath msgPath;
	dormant_node_info msgDninfo;
	int32 input_id = 0;
	bool isAudio = (type == B_MEDIA_RAW_AUDIO)
		|| (type == B_MEDIA_ENCODED_AUDIO);

	{
		BAutolock locker(fSettingsLock);
		for (int32 i = 0; i < fMsgList.CountItems(); i++) {
			BMessage *msg = (BMessage *)fMsgList.ItemAt(i);
			int32 msgType;
			if (msg->FindInt32(kDefaultManagerType, &msgType) == B_OK
				&& ((uint32)msgType == default_type)) {
				haveSaved = true;
				const char *name = NULL;
				const char *path = NULL;
				msg->FindInt32(kDefaultManagerAddon, &msgDninfo.addon);
				msg->FindInt32(kDefaultManagerFlavorId, &msgDninfo.flavor_id);
				msg->FindInt32(kDefaultManagerInput, &input_id);
				msg->FindString(kDefaultManagerFlavorName, &name);
				msg->FindString(kDefaultManagerPath, &path);
				if (name)
					strlcpy(msgDninfo.name, name, sizeof(msgDninfo.name));
				if (path)
					msgPath = BPath(path);
				break;
			}
		}
	}
	if (savedOnly && !haveSaved)
		return false;

	format.type = type;
	count = MAX_NODE_INFOS;
	rv = fRoster->GetLiveNodes(&info[0], &count,
		isInput ? NULL : &format, isInput ? &format : NULL, NULL,
		isInput ? B_BUFFER_PRODUCER | B_PHYSICAL_INPUT
			: B_BUFFER_CONSUMER | B_PHYSICAL_OUTPUT);
	if (rv != B_OK || count < 1) {
		TRACE("Couldn't find physical %s %s node\n",
			isAudio ? "audio" : "video", isInput ? "input" : "output");
		return false;
	}
	for (int i = 0; i < count; i++)
		TRACE("info[%d].name %s\n", i, info[i].name);

	// The audio of a graphics card's HDMI and DisplayPort connectors comes
	// last: it is silent unless a display with speakers is connected, and it
	// is often found before the speakers. (With a saved default the order
	// does not matter, as only the saved device is taken.)
	// On Linux the PipeWire node add-on's nodes that follow PipeWire's
	// default sink and source come first: without a saved default, the
	// Media Kit plays and records where the rest of the desktop does.
	if (isAudio) {
		const char* pipeWireDefault = isInput
			? PIPEWIRE_DEFAULT_INPUT_NAME : PIPEWIRE_DEFAULT_OUTPUT_NAME;
		live_node_info ordered[MAX_NODE_INFOS];
		int orderedCount = 0;
		for (int pass = 0; pass < 3; pass++) {
			for (int i = 0; i < count; i++) {
				int rank = 1;
				if (strcmp(info[i].name, pipeWireDefault) == 0)
					rank = 0;
				else if (!isInput && strstr(info[i].name, "(HDMI)") != NULL)
					rank = 2;
				if (rank == pass)
					ordered[orderedCount++] = info[i];
			}
		}
		for (int i = 0; i < count; i++)
			info[i] = ordered[i];
	}

	// The first round looks for the saved default; when that is not there
	// (a USB headset that is not plugged in, a sound card that was renamed,
	// a camera that is gone), the second round takes what there is: silence
	// until the user chooses again helps nobody.
	for (int round = 0; round < 2; round++) {
		bool useSaved = haveSaved && round == 0;
		if (round == 1 && (!haveSaved || savedOnly))
			break;

		// a saved device of the same name, but another flavor ID
		int sameNameIndex = -1;

		for (int i = 0; i < count; i++) {
			if (isAudio) {
				if (isInput) {
					if (0 == strcmp(info[i].name, "None In")) {
						// we keep the Null audio driver if none else matchs
						if (!savedOnly)
							*id = info[i].node.node;
						continue;
					}
					// skip the Firewire audio driver
					if (0 == strcmp(info[i].name, "DV Input"))
						continue;
				} else {
					if (0 == strcmp(info[i].name, "None Out")) {
						// we keep the Null audio driver if none else matchs
						if (!savedOnly) {
							*id = info[i].node.node;
							if (useSaved)
								fPhysicalAudioOutInputID = input_id;
						}
						continue;
					}
					// skip the Firewire audio driver
					if (0 == strcmp(info[i].name, "DV Output"))
						continue;
				}
			}
			if (useSaved) {	// we have a default info msg
				dormant_node_info dninfo;
				if (fRoster->GetDormantNodeFor(info[i].node,
						&dninfo) != B_OK) {
					ERROR("Couldn't GetDormantNodeFor\n");
					continue;
				}
				// (A default saved before the HDA driver called its HDMI
				// devices "HD Audio (HDMI)" has their old name, "HD Audio".)
				size_t savedLength = strlen(msgDninfo.name);
				bool sameName = strcmp(dninfo.name, msgDninfo.name) == 0
					|| (isAudio && strncmp(dninfo.name, msgDninfo.name,
							savedLength) == 0
						&& strcmp(dninfo.name + savedLength, " (HDMI)") == 0);
				if (!sameName) {
					ERROR("Doesn't match name\n");
					continue;
				}
				BPath path;
				if (gDormantNodeManager->FindAddOnPath(&path, dninfo.addon)
						!= B_OK || path != msgPath) {
					ERROR("Doesn't match : path\n");
					continue;
				}
				// The flavor IDs of devices that are plugged in, cameras and
				// USB audio, are given out in the order they turn up, so
				// they differ from one boot to the next: the saved ID only
				// tells devices of the same name apart.
				if (isAudio && dninfo.flavor_id != msgDninfo.flavor_id) {
					if (sameNameIndex < 0)
						sameNameIndex = i;
					continue;
				}
			}
			TRACE("Default physical %s %s \"%s\" created!\n",
				isAudio ? "audio" : "video", isInput ? "input" : "output",
				info[i].name);
			*id = info[i].node.node;
			if (useSaved && isAudio && !isInput)
				fPhysicalAudioOutInputID = input_id;
			return useSaved;
		}

		if (sameNameIndex >= 0) {
			TRACE("Default physical %s %s \"%s\" created!\n",
				isAudio ? "audio" : "video", isInput ? "input" : "output",
				info[sameNameIndex].name);
			*id = info[sameNameIndex].node.node;
			if (!isInput)
				fPhysicalAudioOutInputID = input_id;
			return true;
		}
	}
	return false;
}


void
DefaultManager::_FindTimeSource()
{
	live_node_info info[MAX_NODE_INFOS];
	media_format input; /* a physical audio output has a logical data input (DAC)*/
	int32 count;
	status_t rv;

	/* First try to use the current default physical audio out
	 */
	if (fPhysicalAudioOut != -1) {
		media_node clone;
		if (fRoster->GetNodeFor(fPhysicalAudioOut,
				&clone) == B_OK) {
			if (clone.kind & B_TIME_SOURCE) {
				fTimeSource = clone.node;
				fRoster->StartTimeSource(clone,
					system_time() + 1000);
				fRoster->ReleaseNode(clone);
				TRACE("Default DAC timesource created!\n");
				return;
			}
			fRoster->ReleaseNode(clone);
		} else {
			TRACE("Default DAC is not a timesource!\n");
		}
	} else {
		TRACE("Default DAC node does not exist!\n");
	}

	/* Now try to find another physical audio out node
	 */
	input.type = B_MEDIA_RAW_AUDIO;
	count = MAX_NODE_INFOS;
	rv = fRoster->GetLiveNodes(&info[0], &count, &input, NULL, NULL,
		B_TIME_SOURCE | B_PHYSICAL_OUTPUT);
	if (rv == B_OK && count >= 1) {
		for (int i = 0; i < count; i++)
			printf("info[%d].name %s\n", i, info[i].name);

		for (int i = 0; i < count; i++) {
			// The BeOS R5 None Out node pretend to be a physical time source,
			// that is pretty dumb
			// skip the Null audio driver
			if (0 == strcmp(info[i].name, "None Out"))
				continue;
			// skip the Firewire audio driver
			if (0 != strstr(info[i].name, "DV Output"))
				continue;
			TRACE("Default DAC timesource \"%s\" created!\n", info[i].name);
			fTimeSource = info[i].node.node;
			fRoster->StartTimeSource(info[i].node,
				system_time() + 1000);
			return;
		}
	} else {
		TRACE("Couldn't find DAC timesource node\n");
	}

	/* XXX we might use other audio or video clock timesources
	 */
}


void
DefaultManager::_FindAudioMixer()
{
	live_node_info info;
	int32 count;
	status_t rv;

	if (fRoster == NULL)
		fRoster = BMediaRoster::Roster();

	count = 1;
	rv = fRoster->GetLiveNodes(&info, &count, NULL, NULL, NULL,
		B_BUFFER_PRODUCER | B_BUFFER_CONSUMER | B_SYSTEM_MIXER);
	if (rv != B_OK || count != 1) {
		TRACE("Couldn't find audio mixer node\n");
		return;
	}
	fAudioMixer = info.node.node;
	TRACE("Default audio mixer node created\n");
}


status_t
DefaultManager::_ConnectMixerToOutput()
{
	if (fPhysicalAudioOutFromMixer.node != media_node::null) {
		// Disconnect the old output. (The mixer should be in auto-stop mode,
		// and stop the output on its own.)
		status_t status = fRoster->Disconnect(fMixerToPhysicalAudioOut,
			fPhysicalAudioOutFromMixer);
		if (status != B_OK)
			return status;

		fMixerToPhysicalAudioOut.node = media_node::null;
		fPhysicalAudioOutFromMixer.node = media_node::null;

		// Find and start the new timesource.
		_FindTimeSource();
	}

	media_node timesource, mixer, soundcard;
	media_input inputs[MAX_INPUT_INFOS];
	media_input input;
	media_output output;
	media_input newinput;
	media_output newoutput;
	media_format format;
	int32 count;

	if (fRoster == NULL)
		fRoster = BMediaRoster::Roster();

	status_t rv = fRoster->GetNodeFor(fPhysicalAudioOut, &soundcard);
	if (rv != B_OK) {
		TRACE("DefaultManager: failed to find soundcard (physical audio "
			"output)\n");
		return B_ERROR;
	}

	rv = fRoster->GetNodeFor(fAudioMixer, &mixer);
	if (rv != B_OK) {
		fRoster->ReleaseNode(soundcard);
		TRACE("DefaultManager: failed to find mixer\n");
		return B_ERROR;
	}

	// we now have the mixer and soundcard nodes,
	// find a free input/output and connect them

	fRoster->GetTimeSource(&timesource);
	fRoster->SetTimeSourceFor(soundcard.node, timesource.node);

	rv = fRoster->GetFreeOutputsFor(mixer, &output, 1, &count,
		B_MEDIA_RAW_AUDIO);
	if (rv != B_OK || count != 1) {
		TRACE("DefaultManager: can't find free mixer output\n");
		rv = B_ERROR;
		goto finish;
	}

	rv = fRoster->GetFreeInputsFor(soundcard, inputs, MAX_INPUT_INFOS, &count,
		B_MEDIA_RAW_AUDIO);
	if (rv != B_OK || count < 1) {
		TRACE("DefaultManager: can't find free soundcard inputs\n");
		rv = B_ERROR;
		goto finish;
	}

	for (int32 i = 0; i < count; i++) {
		input = inputs[i];
		if (input.destination.id == fPhysicalAudioOutInputID)
			break;
	}

	for (int i = 0; i < 6; i++) {
		switch (i) {
			case 0:
				TRACE("DefaultManager: Trying connect in native format (1)\n");
				if (fRoster->GetFormatFor(input, &format) != B_OK) {
					ERROR("DefaultManager: GetFormatFor failed\n");
					continue;
				}
				// XXX BeOS R5 multiaudio node bug workaround
				if (format.u.raw_audio.channel_count == 1) {
					TRACE("##### WARNING! DefaultManager: ignored mono format\n");
					continue;
				}
				break;

			case 1:
				TRACE("DefaultManager: Trying connect in format 1\n");
				format.Clear();
				format.type = B_MEDIA_RAW_AUDIO;
				format.u.raw_audio.frame_rate = 44100;
				format.u.raw_audio.channel_count = 2;
				format.u.raw_audio.format = 0x2;
				break;

			case 2:
				TRACE("DefaultManager: Trying connect in format 2\n");
				format.Clear();
				format.type = B_MEDIA_RAW_AUDIO;
				format.u.raw_audio.frame_rate = 48000;
				format.u.raw_audio.channel_count = 2;
				format.u.raw_audio.format = 0x2;
				break;

			case 3:
				TRACE("DefaultManager: Trying connect in format 3\n");
				format.Clear();
				format.type = B_MEDIA_RAW_AUDIO;
				break;

			case 4:
				// BeOS R5 multiaudio node bug workaround
				TRACE("DefaultManager: Trying connect in native format (2)\n");
				if (fRoster->GetFormatFor(input, &format) != B_OK) {
					ERROR("DefaultManager: GetFormatFor failed\n");
					continue;
				}
				break;

			case 5:
				TRACE("DefaultManager: Trying connect in format 4\n");
				format.Clear();
				break;

		}
		rv = fRoster->Connect(output.source, input.destination, &format,
			&newoutput, &newinput);
		if (rv == B_OK)
			break;
	}
	if (rv != B_OK) {
		ERROR("DefaultManager: connect failed\n");
		goto finish;
	}

	fRoster->SetRunModeNode(mixer, BMediaNode::B_INCREASE_LATENCY);
	fRoster->SetRunModeNode(soundcard, BMediaNode::B_RECORDING);

	fRoster->PrerollNode(mixer);
	fRoster->PrerollNode(soundcard);

	{
		BTimeSource* ts = fRoster->MakeTimeSourceFor(mixer);
		bigtime_t startAt;
		if (ts == NULL) {
			rv = B_ERROR;
			goto finish;
		}

		startAt = ts->Now() + 50000;
		fRoster->StartNode(mixer, startAt);

		// The mixer will be in "auto-stop" mode, so it'll start the output on its own.
		ts->Release();
	}

	fMixerToPhysicalAudioOut = newoutput;
	fPhysicalAudioOutFromMixer = newinput;

finish:
	fRoster->ReleaseNode(mixer);
	fRoster->ReleaseNode(soundcard);
	fRoster->ReleaseNode(timesource);
	return rv;
}


void
DefaultManager::Dump()
{
}


void
DefaultManager::CleanupTeam(team_id team)
{
}


/*!	A default node that is gone (a camera that was unplugged, the sound card
	node or the mixer of a media_addon_server that died) must not stay the
	default: the next rescan then looks for another one. A gone mixer or
	sound card also takes the mixer connection with it; the rescan makes a
	new one and must not try to disconnect the old one first.
*/
void
DefaultManager::NodeUnregistered(media_node_id node)
{
	bool lost = false;
	if (fPhysicalVideoIn == node) {
		fPhysicalVideoIn = -1;
		lost = true;
	}
	if (fPhysicalVideoOut == node) {
		fPhysicalVideoOut = -1;
		lost = true;
	}
	if (fPhysicalAudioIn == node) {
		fPhysicalAudioIn = -1;
		lost = true;
	}
	if (fTimeSource == node)
		fTimeSource = -1;

	bool connectionLost = false;
	if (fPhysicalAudioOut == node) {
		fPhysicalAudioOut = -1;
		connectionLost = true;
	}
	if (fAudioMixer == node) {
		fAudioMixer = -1;
		connectionLost = true;
	}
	if (connectionLost) {
		fMixerConnected = false;
		fMixerToPhysicalAudioOut.node = media_node::null;
		fPhysicalAudioOutFromMixer.node = media_node::null;
	}

	// A default that is gone is replaced at once (the media_addon_server
	// asks for a rescan when a device of an add-on goes, but not when a
	// whole add-on does); the rescan runs in its own thread.
	if (lost || connectionLost)
		Rescan();
}
