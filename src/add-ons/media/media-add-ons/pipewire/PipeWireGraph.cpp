/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PipeWireGraph.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>


static const size_t kRingFrames = 8192;
	// about 170 ms at 48 kHz


static uint32_t
next_power_of_two(uint32_t value)
{
	uint32_t power = 1;
	while (power < value)
		power <<= 1;
	return power;
}


// #pragma mark - PipeWireGraph


PipeWireGraph::PipeWireGraph()
	:
	fLoop(NULL),
	fContext(NULL),
	fCore(NULL),
	fRegistry(NULL),
	fHook(NULL),
	fHookCookie(NULL),
	fPendingSeq(0),
	fSynced(false),
	fCoreFailed(false)
{
	memset(&fRegistryListener, 0, sizeof(fRegistryListener));
	memset(&fCoreListener, 0, sizeof(fCoreListener));
}


PipeWireGraph::~PipeWireGraph()
{
	Uninit();
}


int
PipeWireGraph::Init(const char* clientName)
{
	pw_init(NULL, NULL);

	fLoop = pw_thread_loop_new(clientName, NULL);
	if (fLoop == NULL)
		return -ENOMEM;

	fContext = pw_context_new(pw_thread_loop_get_loop(fLoop), NULL, 0);
	if (fContext == NULL) {
		Uninit();
		return -ENOMEM;
	}

	if (pw_thread_loop_start(fLoop) < 0) {
		Uninit();
		return -EIO;
	}

	pw_thread_loop_lock(fLoop);

	fCore = pw_context_connect(fContext,
		pw_properties_new(PW_KEY_APP_NAME, clientName, NULL), 0);
	if (fCore == NULL) {
		int error = -errno;
		pw_thread_loop_unlock(fLoop);
		Uninit();
		return error != 0 ? error : -ECONNREFUSED;
	}

	static pw_core_events coreEvents;
	memset(&coreEvents, 0, sizeof(coreEvents));
	coreEvents.version = PW_VERSION_CORE_EVENTS;
	coreEvents.done = &_CoreDone;
	coreEvents.error = &_CoreError;
	pw_core_add_listener(fCore, &fCoreListener, &coreEvents, this);

	static pw_registry_events registryEvents;
	memset(&registryEvents, 0, sizeof(registryEvents));
	registryEvents.version = PW_VERSION_REGISTRY_EVENTS;
	registryEvents.global = &_Global;
	registryEvents.global_remove = &_GlobalRemove;
	fRegistry = pw_core_get_registry(fCore, PW_VERSION_REGISTRY, 0);
	pw_registry_add_listener(fRegistry, &fRegistryListener, &registryEvents,
		this);

	pw_thread_loop_unlock(fLoop);
	return 0;
}


void
PipeWireGraph::Uninit()
{
	if (fLoop != NULL) {
		pw_thread_loop_lock(fLoop);
		if (fRegistry != NULL) {
			spa_hook_remove(&fRegistryListener);
			pw_proxy_destroy((pw_proxy*)fRegistry);
			fRegistry = NULL;
		}
		if (fCore != NULL) {
			spa_hook_remove(&fCoreListener);
			pw_core_disconnect(fCore);
			fCore = NULL;
		}
		pw_thread_loop_unlock(fLoop);
		pw_thread_loop_stop(fLoop);
	}
	if (fContext != NULL) {
		pw_context_destroy(fContext);
		fContext = NULL;
	}
	if (fLoop != NULL) {
		pw_thread_loop_destroy(fLoop);
		fLoop = NULL;
	}
}


int
PipeWireGraph::Sync(int timeoutMS)
{
	if (fCore == NULL)
		return -ENOTCONN;

	pw_thread_loop_lock(fLoop);
	fSynced = false;
	fPendingSeq = pw_core_sync(fCore, PW_ID_CORE, fPendingSeq);

	int waited = 0;
	while (!fSynced && !fCoreFailed && waited < timeoutMS) {
		// (the thread loop waits in whole seconds)
		pw_thread_loop_timed_wait(fLoop, 1);
		waited += 1000;
	}
	int result = fSynced ? 0 : fCoreFailed ? -ECONNRESET : -ETIMEDOUT;
	pw_thread_loop_unlock(fLoop);
	return result;
}


void
PipeWireGraph::GetEndpoints(std::vector<PipeWireEndpoint>& endpoints)
{
	if (fLoop == NULL) {
		endpoints.clear();
		return;
	}
	pw_thread_loop_lock(fLoop);
	endpoints = fEndpoints;
	pw_thread_loop_unlock(fLoop);
}


void
PipeWireGraph::SetChangeHook(change_hook hook, void* cookie)
{
	if (fLoop != NULL)
		pw_thread_loop_lock(fLoop);
	fHook = hook;
	fHookCookie = cookie;
	if (fLoop != NULL)
		pw_thread_loop_unlock(fLoop);
}


/*static*/ void
PipeWireGraph::_Global(void* data, uint32_t id, uint32_t permissions,
	const char* type, uint32_t version, const struct spa_dict* props)
{
	PipeWireGraph* graph = (PipeWireGraph*)data;
	if (props == NULL || strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
		return;

	const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
	if (mediaClass == NULL)
		return;
	bool sink = strcmp(mediaClass, "Audio/Sink") == 0;
	if (!sink && strcmp(mediaClass, "Audio/Source") != 0)
		return;

	const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
	const char* description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
	if (name == NULL)
		return;

	PipeWireEndpoint endpoint;
	endpoint.id = id;
	endpoint.name = name;
	endpoint.description = description != NULL ? description : name;
	endpoint.sink = sink;
	graph->fEndpoints.push_back(endpoint);
	graph->_Changed();
}


/*static*/ void
PipeWireGraph::_GlobalRemove(void* data, uint32_t id)
{
	PipeWireGraph* graph = (PipeWireGraph*)data;
	for (size_t i = 0; i < graph->fEndpoints.size(); i++) {
		if (graph->fEndpoints[i].id == id) {
			graph->fEndpoints.erase(graph->fEndpoints.begin() + i);
			graph->_Changed();
			return;
		}
	}
}


/*static*/ void
PipeWireGraph::_CoreDone(void* data, uint32_t id, int seq)
{
	PipeWireGraph* graph = (PipeWireGraph*)data;
	if (id == PW_ID_CORE && seq == graph->fPendingSeq) {
		graph->fSynced = true;
		pw_thread_loop_signal(graph->fLoop, false);
	}
}


/*static*/ void
PipeWireGraph::_CoreError(void* data, uint32_t id, int seq, int res,
	const char* message)
{
	PipeWireGraph* graph = (PipeWireGraph*)data;
	fprintf(stderr, "pipewire media add-on: error %d on %u: %s\n", res, id,
		message);
	if (id == PW_ID_CORE && res == -EPIPE) {
		graph->fCoreFailed = true;
		pw_thread_loop_signal(graph->fLoop, false);
	}
}


void
PipeWireGraph::_Changed()
{
	if (fHook != NULL)
		fHook(fHookCookie);
}


// #pragma mark - PipeWireStream


PipeWireStream::PipeWireStream(PipeWireGraph* graph, bool playback,
	const char* target, uint32_t rate, uint32_t channels,
	const char* nodeName)
	:
	fGraph(graph),
	fPlayback(playback),
	fTarget(target != NULL ? target : ""),
	fNodeName(nodeName),
	fRate(rate),
	fChannels(channels),
	fStream(NULL),
	fRingFrames(kRingFrames),
	fUnderruns(0)
{
	memset(&fListener, 0, sizeof(fListener));
	spa_ringbuffer_init(&fRing);
	fRingBytes = next_power_of_two(kRingFrames * channels * sizeof(float));
	fRingFrames = fRingBytes / (channels * sizeof(float));
	fRingData = (float*)calloc(1, fRingBytes);
}


PipeWireStream::~PipeWireStream()
{
	Disconnect();
	free(fRingData);
}


int
PipeWireStream::Connect()
{
	if (fRingData == NULL)
		return -ENOMEM;
	if (fGraph->Core() == NULL)
		return -ENOTCONN;

	pw_thread_loop_lock(fGraph->Loop());

	pw_properties* props = pw_properties_new(
		PW_KEY_MEDIA_TYPE, "Audio",
		PW_KEY_MEDIA_CATEGORY, fPlayback ? "Playback" : "Capture",
		PW_KEY_MEDIA_ROLE, "Music",
		PW_KEY_NODE_NAME, fNodeName.c_str(),
		PW_KEY_APP_NAME, "air/OS Media Kit",
		NULL);
	if (!fTarget.empty())
		pw_properties_set(props, PW_KEY_TARGET_OBJECT, fTarget.c_str());

	fStream = pw_stream_new(fGraph->Core(), fNodeName.c_str(), props);
	if (fStream == NULL) {
		pw_thread_loop_unlock(fGraph->Loop());
		return -ENOMEM;
	}

	static pw_stream_events events;
	memset(&events, 0, sizeof(events));
	events.version = PW_VERSION_STREAM_EVENTS;
	events.state_changed = &_StateChanged;
	events.process = &_Process;
	pw_stream_add_listener(fStream, &fListener, &events, this);

	uint8_t buffer[1024];
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer,
		sizeof(buffer));
	struct spa_audio_info_raw info;
	memset(&info, 0, sizeof(info));
	info.format = SPA_AUDIO_FORMAT_F32;
	info.rate = fRate;
	info.channels = fChannels;
	if (fChannels == 1)
		info.position[0] = SPA_AUDIO_CHANNEL_MONO;
	else if (fChannels == 2) {
		info.position[0] = SPA_AUDIO_CHANNEL_FL;
		info.position[1] = SPA_AUDIO_CHANNEL_FR;
	} else
		info.flags = SPA_AUDIO_FLAG_UNPOSITIONED;
	const struct spa_pod* params[1] = {
		spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)
	};

	int status = pw_stream_connect(fStream,
		fPlayback ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT, PW_ID_ANY,
		(enum pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT
			| PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
		params, 1);
	if (status < 0) {
		spa_hook_remove(&fListener);
		pw_stream_destroy(fStream);
		fStream = NULL;
	}

	pw_thread_loop_unlock(fGraph->Loop());
	return status < 0 ? status : 0;
}


void
PipeWireStream::Disconnect()
{
	if (fStream == NULL)
		return;

	pw_thread_loop_lock(fGraph->Loop());
	spa_hook_remove(&fListener);
	pw_stream_destroy(fStream);
	fStream = NULL;
	pw_thread_loop_unlock(fGraph->Loop());

	spa_ringbuffer_init(&fRing);
}


size_t
PipeWireStream::Write(const float* frames, size_t count)
{
	const uint32_t frameBytes = fChannels * sizeof(float);
	uint32_t index;
	int32_t filled = spa_ringbuffer_get_write_index(&fRing, &index);
	if (filled < 0)
		filled = 0;
	uint32_t space = (fRingBytes - (uint32_t)filled) / frameBytes;
	if (count > space)
		count = space;
	if (count == 0)
		return 0;

	uint32_t bytes = count * frameBytes;
	spa_ringbuffer_write_data(&fRing, fRingData, fRingBytes,
		index & (fRingBytes - 1), frames, bytes);
	spa_ringbuffer_write_update(&fRing, index + bytes);
	return count;
}


size_t
PipeWireStream::Read(float* frames, size_t count)
{
	const uint32_t frameBytes = fChannels * sizeof(float);
	uint32_t index;
	int32_t filled = spa_ringbuffer_get_read_index(&fRing, &index);
	if (filled <= 0)
		return 0;
	uint32_t available = (uint32_t)filled / frameBytes;
	if (count > available)
		count = available;

	uint32_t bytes = count * frameBytes;
	spa_ringbuffer_read_data(&fRing, fRingData, fRingBytes,
		index & (fRingBytes - 1), frames, bytes);
	spa_ringbuffer_read_update(&fRing, index + bytes);
	return count;
}


size_t
PipeWireStream::Queued()
{
	uint32_t index;
	int32_t filled = spa_ringbuffer_get_read_index(&fRing, &index);
	return filled > 0 ? filled / (fChannels * sizeof(float)) : 0;
}


/*static*/ void
PipeWireStream::_Process(void* data)
{
	PipeWireStream* stream = (PipeWireStream*)data;
	struct pw_buffer* buffer = pw_stream_dequeue_buffer(stream->fStream);
	if (buffer == NULL)
		return;

	struct spa_buffer* spaBuffer = buffer->buffer;
	struct spa_data& spaData = spaBuffer->datas[0];
	if (spaData.data == NULL) {
		pw_stream_queue_buffer(stream->fStream, buffer);
		return;
	}

	const uint32_t frameBytes = stream->fChannels * sizeof(float);
	if (stream->fPlayback) {
		uint32_t frames = spaData.maxsize / frameBytes;
		if (buffer->requested != 0 && buffer->requested < frames)
			frames = buffer->requested;

		float* out = (float*)spaData.data;
		size_t got = stream->Read(out, frames);
		if (got < frames) {
			memset(out + got * stream->fChannels, 0,
				(frames - got) * frameBytes);
			if (got > 0)
				stream->fUnderruns++;
		}

		spaData.chunk->offset = 0;
		spaData.chunk->stride = frameBytes;
		spaData.chunk->size = frames * frameBytes;
	} else {
		uint32_t offset = SPA_MIN(spaData.chunk->offset, spaData.maxsize);
		uint32_t size = SPA_MIN(spaData.chunk->size, spaData.maxsize - offset);
		// (a full ring drops what does not fit: the node fell behind)
		stream->Write((const float*)((uint8_t*)spaData.data + offset),
			size / frameBytes);
	}

	pw_stream_queue_buffer(stream->fStream, buffer);
}


/*static*/ void
PipeWireStream::_StateChanged(void* data, enum pw_stream_state oldState,
	enum pw_stream_state state, const char* error)
{
	PipeWireStream* stream = (PipeWireStream*)data;
	if (state == PW_STREAM_STATE_ERROR) {
		fprintf(stderr, "pipewire media add-on: stream %s: %s\n",
			stream->fNodeName.c_str(), error != NULL ? error : "error");
	}
}
