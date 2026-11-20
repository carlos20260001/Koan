/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef VOS_PIPEWIRE_MEDIA_ADD_ON_H
#define VOS_PIPEWIRE_MEDIA_ADD_ON_H


#include <vector>

#include <Locker.h>
#include <MediaAddOn.h>
#include <MediaDefs.h>
#include <String.h>

#include "PipeWireGraph.h"


/*!	The classic Media Kit's sound hardware on Linux: PipeWire's sinks and
	sources as physical output and input nodes, in place of Haiku's
	multi_audio driver nodes. Two flavors follow PipeWire's default sink and
	source (and so the desktop's volume control and device choice); one
	flavor per sink and source plays to or records from that device. */
class PipeWireAddOn : public BMediaAddOn {
public:
								PipeWireAddOn(image_id image);
	virtual						~PipeWireAddOn();

	virtual	status_t			InitCheck(const char** _failureText);
	virtual	int32				CountFlavors();
	virtual	status_t			GetFlavorAt(int32 index,
									const flavor_info** _info);
	virtual	BMediaNode*			InstantiateNodeFor(const flavor_info* info,
									BMessage* config, status_t* _error);
	virtual	status_t			GetConfigurationFor(BMediaNode* node,
									BMessage* message);
	virtual	bool				WantsAutoStart();
	virtual	status_t			AutoStart(int index, BMediaNode** _node,
									int32* _internalID, bool* _hasMore);

			PipeWireGraph*		Graph() { return &fGraph; }

	static	void				GetFormat(media_format& format);

private:
	struct Flavor {
		flavor_info				info;
		BString					name;
		BString					target;
			// node.name; empty: PipeWire's default
		bool					output;
	};

	static	void				_GraphChanged(void* cookie);
			void				_UpdateFlavors();

			PipeWireGraph		fGraph;
			status_t			fInitStatus;
			BLocker				fLock;
			std::vector<Flavor*> fFlavors;
			std::vector<Flavor*> fOldFlavors;
			media_format		fFormat;
};


#endif	// VOS_PIPEWIRE_MEDIA_ADD_ON_H
