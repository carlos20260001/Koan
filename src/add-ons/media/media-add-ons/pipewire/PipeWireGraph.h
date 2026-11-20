/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef VOS_PIPEWIRE_MEDIA_GRAPH_H
#define VOS_PIPEWIRE_MEDIA_GRAPH_H


#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include <pipewire/pipewire.h>
#include <spa/utils/ringbuffer.h>


/*!	A PipeWire audio sink ("Audio/Sink") or source ("Audio/Source"). */
struct PipeWireEndpoint {
	uint32_t		id;
		// PipeWire's global ID, the same while PipeWire runs
	std::string		name;
		// node.name: what a stream asks for as its target
	std::string		description;
		// node.description: what the user sees
	bool			sink;
};


/*!	The connection to PipeWire: a thread loop, the core, and the sinks and
	sources the registry announces. Independent of the Media Kit, so that
	it can be tested on its own. */
class PipeWireGraph {
public:
	typedef void (*change_hook)(void* cookie);

								PipeWireGraph();
								~PipeWireGraph();

			int					Init(const char* clientName);
				// 0, or a negative errno
			void				Uninit();

			int					Sync(int timeoutMS);
				// waits until the server processed everything sent so far
				// (and the registry announced what it has); 0 or -errno
			void				GetEndpoints(
									std::vector<PipeWireEndpoint>& endpoints);
			void				SetChangeHook(change_hook hook, void* cookie);
				// called on PipeWire's thread when sinks or sources come and
				// go

			pw_thread_loop*		Loop() const { return fLoop; }
			pw_core*			Core() const { return fCore; }

private:
	static	void				_Global(void* data, uint32_t id,
									uint32_t permissions, const char* type,
									uint32_t version,
									const struct spa_dict* props);
	static	void				_GlobalRemove(void* data, uint32_t id);
	static	void				_CoreDone(void* data, uint32_t id, int seq);
	static	void				_CoreError(void* data, uint32_t id, int seq,
									int res, const char* message);

			void				_Changed();

			pw_thread_loop*		fLoop;
			pw_context*			fContext;
			pw_core*			fCore;
			pw_registry*		fRegistry;
			struct spa_hook		fRegistryListener;
			struct spa_hook		fCoreListener;
			std::vector<PipeWireEndpoint> fEndpoints;
			change_hook			fHook;
			void*				fHookCookie;
			int					fPendingSeq;
			bool				fSynced;
			bool				fCoreFailed;
};


/*!	A stream of interleaved 32-bit float frames to a sink (playback) or
	from a source (capture), through a lock-free ring buffer: one thread of
	the media node writes or reads it, PipeWire's data thread the other
	end. */
class PipeWireStream {
public:
								PipeWireStream(PipeWireGraph* graph,
									bool playback, const char* target,
									uint32_t rate, uint32_t channels,
									const char* nodeName);
								~PipeWireStream();

			int					Connect();
				// 0, or a negative errno
			void				Disconnect();

			size_t				Write(const float* frames, size_t count);
				// playback: returns how many frames fit
			size_t				Read(float* frames, size_t count);
				// capture: returns how many frames there were
			size_t				Queued();
				// frames in the ring
			size_t				RingFrames() const { return fRingFrames; }

			uint32_t			Rate() const { return fRate; }
			uint32_t			Channels() const { return fChannels; }

			uint64_t			Underruns() const { return fUnderruns; }

private:
	static	void				_Process(void* data);
	static	void				_StateChanged(void* data,
									enum pw_stream_state oldState,
									enum pw_stream_state state,
									const char* error);

			PipeWireGraph*		fGraph;
			bool				fPlayback;
			std::string			fTarget;
			std::string			fNodeName;
			uint32_t			fRate;
			uint32_t			fChannels;
			pw_stream*			fStream;
			struct spa_hook		fListener;

			struct spa_ringbuffer fRing;
			float*				fRingData;
			size_t				fRingFrames;
			uint32_t			fRingBytes;
	volatile uint64_t			fUnderruns;
};


#endif	// VOS_PIPEWIRE_MEDIA_GRAPH_H
