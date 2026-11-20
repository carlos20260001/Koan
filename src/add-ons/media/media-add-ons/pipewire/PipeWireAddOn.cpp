/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PipeWireAddOn.h"

#include <stdio.h>
#include <string.h>

#include <Autolock.h>

#include <PipeWireNodes.h>

#include "PipeWireNode.h"


// flavor IDs: the two defaults, then 16 + PipeWire's ID of the device
static const int32 kDefaultOutputFlavor = 0;
static const int32 kDefaultInputFlavor = 1;
static const int32 kDeviceFlavorBase = 16;


extern "C" _EXPORT BMediaAddOn*
make_media_addon(image_id image)
{
	return new PipeWireAddOn(image);
}


PipeWireAddOn::PipeWireAddOn(image_id image)
	:
	BMediaAddOn(image),
	fInitStatus(B_NO_INIT),
	fLock("pipewire add-on")
{
	GetFormat(fFormat);

	int status = fGraph.Init("air/OS media_addon_server");
	if (status == 0)
		status = fGraph.Sync(3000);
	if (status != 0) {
		fprintf(stderr, "pipewire media add-on: no PipeWire: %s\n",
			strerror(-status));
		fInitStatus = B_ERROR;
		return;
	}
	fInitStatus = B_OK;

	_UpdateFlavors();
	fGraph.SetChangeHook(&_GraphChanged, this);
}


PipeWireAddOn::~PipeWireAddOn()
{
	fGraph.SetChangeHook(NULL, NULL);
	fGraph.Uninit();
	for (size_t i = 0; i < fFlavors.size(); i++)
		delete fFlavors[i];
	for (size_t i = 0; i < fOldFlavors.size(); i++)
		delete fOldFlavors[i];
}


/*static*/ void
PipeWireAddOn::GetFormat(media_format& format)
{
	format.Clear();
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
}


status_t
PipeWireAddOn::InitCheck(const char** _failureText)
{
	if (fInitStatus != B_OK && _failureText != NULL)
		*_failureText = "PipeWire is not running";
	return fInitStatus;
}


int32
PipeWireAddOn::CountFlavors()
{
	BAutolock _(fLock);
	return fFlavors.size();
}


status_t
PipeWireAddOn::GetFlavorAt(int32 index, const flavor_info** _info)
{
	BAutolock _(fLock);
	if (index < 0 || index >= (int32)fFlavors.size())
		return B_BAD_INDEX;
	*_info = &fFlavors[index]->info;
	return B_OK;
}


BMediaNode*
PipeWireAddOn::InstantiateNodeFor(const flavor_info* info, BMessage* config,
	status_t* _error)
{
	BAutolock _(fLock);
	for (size_t i = 0; i < fFlavors.size(); i++) {
		const Flavor* flavor = fFlavors[i];
		if (flavor->info.internal_id != info->internal_id)
			continue;

		BMediaNode* node;
		if (flavor->output) {
			node = new(std::nothrow) PipeWireOutputNode(this,
				flavor->info.internal_id, flavor->name.String(),
				flavor->target.String());
		} else {
			node = new(std::nothrow) PipeWireInputNode(this,
				flavor->info.internal_id, flavor->name.String(),
				flavor->target.String());
		}
		*_error = node != NULL ? B_OK : B_NO_MEMORY;
		return node;
	}

	*_error = B_ENTRY_NOT_FOUND;
	return NULL;
}


status_t
PipeWireAddOn::GetConfigurationFor(BMediaNode* node, BMessage* message)
{
	return B_OK;
}


bool
PipeWireAddOn::WantsAutoStart()
{
	// media_addon_server instantiates the physical inputs and outputs
	return false;
}


status_t
PipeWireAddOn::AutoStart(int index, BMediaNode** _node, int32* _internalID,
	bool* _hasMore)
{
	return B_NOT_SUPPORTED;
}


/*static*/ void
PipeWireAddOn::_GraphChanged(void* cookie)
{
	// (on PipeWire's thread: the flavors are rebuilt there, and
	// media_addon_server, told by a message, asks for them on its own)
	PipeWireAddOn* addOn = (PipeWireAddOn*)cookie;
	addOn->_UpdateFlavors();
	addOn->NotifyFlavorChange();
}


void
PipeWireAddOn::_UpdateFlavors()
{
	std::vector<PipeWireEndpoint> endpoints;
	fGraph.GetEndpoints(endpoints);

	BAutolock _(fLock);
	// (media_addon_server may still hold a flavor_info it got: the old
	// flavors are kept until the add-on goes)
	fOldFlavors.insert(fOldFlavors.end(), fFlavors.begin(), fFlavors.end());
	fFlavors.clear();

	// the two defaults, then the devices; the default input only once
	// there is a source at all
	bool haveSource = false;
	for (size_t i = 0; i < endpoints.size(); i++)
		haveSource |= !endpoints[i].sink;

	for (int pass = 0; pass < 2 + (int)endpoints.size(); pass++) {
		Flavor* flavor = new(std::nothrow) Flavor;
		if (flavor == NULL)
			break;
		memset(&flavor->info, 0, sizeof(flavor->info));

		if (pass == 0) {
			flavor->name = PIPEWIRE_DEFAULT_OUTPUT_NAME;
			flavor->output = true;
			flavor->info.internal_id = kDefaultOutputFlavor;
		} else if (pass == 1) {
			if (!haveSource) {
				delete flavor;
				continue;
			}
			flavor->name = PIPEWIRE_DEFAULT_INPUT_NAME;
			flavor->output = false;
			flavor->info.internal_id = kDefaultInputFlavor;
		} else {
			const PipeWireEndpoint& endpoint = endpoints[pass - 2];
			flavor->name = endpoint.description.c_str();
			flavor->target = endpoint.name.c_str();
			flavor->output = endpoint.sink;
			flavor->info.internal_id = kDeviceFlavorBase + endpoint.id;
		}

		flavor->info.name = flavor->name.String();
		flavor->info.info = flavor->output
			? "Plays through PipeWire." : "Records through PipeWire.";
		flavor->info.flavor_flags = 0;
		flavor->info.possible_count = 1;
		if (flavor->output) {
			flavor->info.kinds = B_BUFFER_CONSUMER | B_PHYSICAL_OUTPUT;
			flavor->info.in_format_count = 1;
			flavor->info.in_formats = &fFormat;
		} else {
			flavor->info.kinds = B_BUFFER_PRODUCER | B_PHYSICAL_INPUT;
			flavor->info.out_format_count = 1;
			flavor->info.out_formats = &fFormat;
		}
		fFlavors.push_back(flavor);
	}
}
