/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PipeWireNode.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <Buffer.h>
#include <BufferGroup.h>
#include <TimeSource.h>

#include "PipeWireAddOn.h"
#include "PipeWireGraph.h"


static const float kRate = 48000;
static const uint32 kChannels = 2;
static const size_t kBufferFrames = 512;
	// about 10 ms
static const bigtime_t kOutputLatency = 40000;
	// the ring's prefill and PipeWire's quantum
static const bigtime_t kInputLatency = 20000;


static void
fill_format(media_format& format, float rate, uint32 channels)
{
	format.Clear();
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio.frame_rate = rate;
	format.u.raw_audio.channel_count = channels;
	format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
	format.u.raw_audio.buffer_size = kBufferFrames * channels * sizeof(float);
}


/*!	Specializes \a format to what the nodes take: any rate and channel
	count (PipeWire converts), 32-bit float, host endian. */
static status_t
specialize_format(media_format& format)
{
	if (format.type == B_MEDIA_NO_TYPE)
		format.type = B_MEDIA_RAW_AUDIO;
	if (format.type != B_MEDIA_RAW_AUDIO)
		return B_MEDIA_BAD_FORMAT;

	media_raw_audio_format& raw = format.u.raw_audio;
	const media_raw_audio_format& wildcard
		= media_raw_audio_format::wildcard;
	if ((raw.format != wildcard.format
			&& raw.format != media_raw_audio_format::B_AUDIO_FLOAT)
		|| (raw.byte_order != wildcard.byte_order
			&& raw.byte_order != B_MEDIA_HOST_ENDIAN)
		|| (raw.channel_count != wildcard.channel_count
			&& (raw.channel_count < 1 || raw.channel_count > 8))) {
		return B_MEDIA_BAD_FORMAT;
	}

	if (raw.frame_rate == wildcard.frame_rate)
		raw.frame_rate = kRate;
	if (raw.channel_count == wildcard.channel_count)
		raw.channel_count = kChannels;
	raw.format = media_raw_audio_format::B_AUDIO_FLOAT;
	raw.byte_order = B_MEDIA_HOST_ENDIAN;
	if (raw.buffer_size == wildcard.buffer_size)
		raw.buffer_size = kBufferFrames * raw.channel_count * sizeof(float);
	return B_OK;
}


// #pragma mark - PipeWireOutputNode


PipeWireOutputNode::PipeWireOutputNode(PipeWireAddOn* addOn, int32 flavorID,
	const char* name, const char* target)
	:
	BMediaNode(name),
	BBufferConsumer(B_MEDIA_RAW_AUDIO),
	BMediaEventLooper(),
	fAddOn(addOn),
	fFlavorID(flavorID),
	fTarget(target),
	fStream(NULL)
{
	AddNodeKind(B_PHYSICAL_OUTPUT);

	fInput.source = media_source::null;
	_GetFormat(fInput.format);
	strlcpy(fInput.name, name, sizeof(fInput.name));
}


PipeWireOutputNode::~PipeWireOutputNode()
{
	Quit();
	delete fStream;
}


void
PipeWireOutputNode::_GetFormat(media_format& format) const
{
	fill_format(format, kRate, kChannels);
	format.u.raw_audio.frame_rate = media_raw_audio_format::wildcard.frame_rate;
	format.u.raw_audio.channel_count
		= media_raw_audio_format::wildcard.channel_count;
}


BMediaAddOn*
PipeWireOutputNode::AddOn(int32* _internalID) const
{
	if (_internalID != NULL)
		*_internalID = fFlavorID;
	return fAddOn;
}


status_t
PipeWireOutputNode::HandleMessage(int32 message, const void* data,
	size_t size)
{
	if (BBufferConsumer::HandleMessage(message, data, size) == B_OK)
		return B_OK;
	return BMediaNode::HandleMessage(message, data, size);
}


void
PipeWireOutputNode::NodeRegistered()
{
	fInput.destination.port = ControlPort();
	fInput.destination.id = 0;
	fInput.node = Node();

	SetPriority(B_REAL_TIME_PRIORITY);
	SetEventLatency(kOutputLatency);
	Run();
}


status_t
PipeWireOutputNode::AcceptFormat(const media_destination& destination,
	media_format* format)
{
	if (destination != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;
	if (specialize_format(*format) != B_OK) {
		fill_format(*format, kRate, kChannels);
		return B_MEDIA_BAD_FORMAT;
	}
	return B_OK;
}


status_t
PipeWireOutputNode::GetNextInput(int32* cookie, media_input* _input)
{
	if (*cookie != 0)
		return B_BAD_INDEX;
	*_input = fInput;
	(*cookie)++;
	return B_OK;
}


void
PipeWireOutputNode::DisposeInputCookie(int32 cookie)
{
}


void
PipeWireOutputNode::BufferReceived(BBuffer* buffer)
{
	if (buffer->Header()->destination != fInput.destination.id) {
		buffer->Recycle();
		return;
	}

	media_timed_event event(buffer->Header()->start_time,
		BTimedEventQueue::B_HANDLE_BUFFER, buffer,
		BTimedEventQueue::B_RECYCLE_BUFFER);
	if (EventQueue()->AddEvent(event) != B_OK)
		buffer->Recycle();
}


void
PipeWireOutputNode::ProducerDataStatus(const media_destination& destination,
	int32 status, bigtime_t performanceTime)
{
}


status_t
PipeWireOutputNode::GetLatencyFor(const media_destination& destination,
	bigtime_t* _latency, media_node_id* _timeSource)
{
	if (destination != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;
	*_latency = EventLatency() + SchedulingLatency();
	*_timeSource = TimeSource()->ID();
	return B_OK;
}


status_t
PipeWireOutputNode::Connected(const media_source& producer,
	const media_destination& destination, const media_format& format,
	media_input* _input)
{
	if (destination != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	media_format specialized = format;
	if (specialize_format(specialized) != B_OK)
		return B_MEDIA_BAD_FORMAT;

	delete fStream;
	BString nodeName("vos-media-kit-");
	nodeName << Name();
	fStream = new(std::nothrow) PipeWireStream(fAddOn->Graph(), true,
		fTarget.String(), (uint32)specialized.u.raw_audio.frame_rate,
		specialized.u.raw_audio.channel_count, nodeName.String());
	if (fStream == NULL)
		return B_NO_MEMORY;
	int status = fStream->Connect();
	if (status != 0) {
		fprintf(stderr, "pipewire media add-on: cannot play to %s: %s\n",
			fTarget.Length() > 0 ? fTarget.String() : "the default sink",
			strerror(-status));
		delete fStream;
		fStream = NULL;
		return B_ERROR;
	}

	fInput.source = producer;
	fInput.format = specialized;
	*_input = fInput;
	return B_OK;
}


void
PipeWireOutputNode::Disconnected(const media_source& producer,
	const media_destination& destination)
{
	if (destination != fInput.destination || producer != fInput.source)
		return;

	EventQueue()->FlushEvents(0, BTimedEventQueue::B_ALWAYS, true,
		BTimedEventQueue::B_HANDLE_BUFFER);
	delete fStream;
	fStream = NULL;
	fInput.source = media_source::null;
	_GetFormat(fInput.format);
}


status_t
PipeWireOutputNode::FormatChanged(const media_source& producer,
	const media_destination& consumer, int32 changeTag,
	const media_format& format)
{
	return B_MEDIA_BAD_FORMAT;
}


void
PipeWireOutputNode::HandleEvent(const media_timed_event* event,
	bigtime_t lateness, bool realTimeEvent)
{
	if (event->type != BTimedEventQueue::B_HANDLE_BUFFER)
		return;

	BBuffer* buffer = (BBuffer*)event->pointer;
	if (fStream != NULL && RunState() == B_STARTED) {
		size_t frames = buffer->SizeUsed()
			/ (fStream->Channels() * sizeof(float));
		// (a full ring drops the rest: the sound card's clock is slower
		// than the time source's)
		fStream->Write((const float*)buffer->Data(), frames);
	}
	buffer->Recycle();
}


// #pragma mark - PipeWireInputNode


PipeWireInputNode::PipeWireInputNode(PipeWireAddOn* addOn, int32 flavorID,
	const char* name, const char* target)
	:
	BMediaNode(name),
	BBufferProducer(B_MEDIA_RAW_AUDIO),
	BMediaEventLooper(),
	fAddOn(addOn),
	fFlavorID(flavorID),
	fTarget(target),
	fStream(NULL),
	fBufferGroup(NULL),
	fOwnBufferGroup(false),
	fEnabled(true),
	fRunning(false),
	fLatency(kInputLatency)
{
	AddNodeKind(B_PHYSICAL_INPUT);

	fOutput.destination = media_destination::null;
	_GetFormat(fOutput.format);
	strlcpy(fOutput.name, name, sizeof(fOutput.name));
}


PipeWireInputNode::~PipeWireInputNode()
{
	Quit();
	delete fStream;
	if (fOwnBufferGroup)
		delete fBufferGroup;
}


void
PipeWireInputNode::_GetFormat(media_format& format) const
{
	fill_format(format, kRate, kChannels);
}


BMediaAddOn*
PipeWireInputNode::AddOn(int32* _internalID) const
{
	if (_internalID != NULL)
		*_internalID = fFlavorID;
	return fAddOn;
}


status_t
PipeWireInputNode::HandleMessage(int32 message, const void* data, size_t size)
{
	if (BBufferProducer::HandleMessage(message, data, size) == B_OK)
		return B_OK;
	return BMediaNode::HandleMessage(message, data, size);
}


void
PipeWireInputNode::NodeRegistered()
{
	fOutput.source.port = ControlPort();
	fOutput.source.id = 0;
	fOutput.node = Node();

	SetPriority(B_REAL_TIME_PRIORITY);
	SetEventLatency(fLatency);
	Run();
}


status_t
PipeWireInputNode::FormatSuggestionRequested(media_type type, int32 quality,
	media_format* format)
{
	if (type != B_MEDIA_RAW_AUDIO && type != B_MEDIA_UNKNOWN_TYPE)
		return B_MEDIA_BAD_FORMAT;
	_GetFormat(*format);
	return B_OK;
}


status_t
PipeWireInputNode::FormatProposal(const media_source& output,
	media_format* format)
{
	if (output != fOutput.source)
		return B_MEDIA_BAD_SOURCE;
	if (specialize_format(*format) != B_OK) {
		_GetFormat(*format);
		return B_MEDIA_BAD_FORMAT;
	}
	return B_OK;
}


status_t
PipeWireInputNode::FormatChangeRequested(const media_source& source,
	const media_destination& destination, media_format* format,
	int32* _deprecated_)
{
	return B_ERROR;
}


status_t
PipeWireInputNode::GetNextOutput(int32* cookie, media_output* _output)
{
	if (*cookie != 0)
		return B_BAD_INDEX;
	*_output = fOutput;
	(*cookie)++;
	return B_OK;
}


status_t
PipeWireInputNode::DisposeOutputCookie(int32 cookie)
{
	return B_OK;
}


status_t
PipeWireInputNode::SetBufferGroup(const media_source& source,
	BBufferGroup* group)
{
	if (source != fOutput.source)
		return B_MEDIA_BAD_SOURCE;
	if (fOwnBufferGroup)
		delete fBufferGroup;
	fBufferGroup = group;
	fOwnBufferGroup = false;
	return B_OK;
}


status_t
PipeWireInputNode::PrepareToConnect(const media_source& what,
	const media_destination& where, media_format* format,
	media_source* _source, char* _name)
{
	if (what != fOutput.source)
		return B_MEDIA_BAD_SOURCE;
	if (fOutput.destination != media_destination::null)
		return B_MEDIA_ALREADY_CONNECTED;
	if (specialize_format(*format) != B_OK)
		return B_MEDIA_BAD_FORMAT;

	fOutput.destination = where;
	fOutput.format = *format;
	*_source = fOutput.source;
	strlcpy(_name, fOutput.name, B_MEDIA_NAME_LENGTH);
	return B_OK;
}


void
PipeWireInputNode::Connect(status_t error, const media_source& source,
	const media_destination& destination, const media_format& format,
	char* ioName)
{
	if (error != B_OK || source != fOutput.source) {
		fOutput.destination = media_destination::null;
		_GetFormat(fOutput.format);
		return;
	}

	fOutput.destination = destination;
	fOutput.format = format;
	strlcpy(ioName, fOutput.name, B_MEDIA_NAME_LENGTH);

	const media_raw_audio_format& raw = format.u.raw_audio;
	delete fStream;
	BString nodeName("vos-media-kit-");
	nodeName << Name();
	fStream = new(std::nothrow) PipeWireStream(fAddOn->Graph(), false,
		fTarget.String(), (uint32)raw.frame_rate, raw.channel_count,
		nodeName.String());
	if (fStream != NULL && fStream->Connect() != 0) {
		fprintf(stderr, "pipewire media add-on: cannot record from %s\n",
			fTarget.Length() > 0 ? fTarget.String() : "the default source");
		delete fStream;
		fStream = NULL;
	}

	if (fBufferGroup == NULL) {
		fBufferGroup = new(std::nothrow) BBufferGroup(raw.buffer_size, 8);
		fOwnBufferGroup = true;
	}

	bigtime_t downstream;
	media_node_id timeSource;
	if (FindLatencyFor(fOutput.destination, &downstream, &timeSource) == B_OK)
		fLatency = kInputLatency + downstream;
	SetEventLatency(fLatency);
}


void
PipeWireInputNode::Disconnect(const media_source& what,
	const media_destination& where)
{
	if (what != fOutput.source || where != fOutput.destination)
		return;

	EventQueue()->FlushEvents(0, BTimedEventQueue::B_ALWAYS, true,
		BTimedEventQueue::B_HANDLE_BUFFER);
	fRunning = false;
	delete fStream;
	fStream = NULL;
	if (fOwnBufferGroup) {
		delete fBufferGroup;
		fBufferGroup = NULL;
		fOwnBufferGroup = false;
	}
	fOutput.destination = media_destination::null;
	_GetFormat(fOutput.format);
}


void
PipeWireInputNode::LateNoticeReceived(const media_source& what,
	bigtime_t howMuch, bigtime_t performanceTime)
{
	if (what != fOutput.source || RunMode() == B_RECORDING)
		return;
	// capture cannot produce earlier: take more latency
	fLatency += howMuch;
	SetEventLatency(fLatency);
}


void
PipeWireInputNode::EnableOutput(const media_source& what, bool enabled,
	int32* _deprecated_)
{
	if (what == fOutput.source)
		fEnabled = enabled;
}


bigtime_t
PipeWireInputNode::_BufferDuration() const
{
	const media_raw_audio_format& raw = fOutput.format.u.raw_audio;
	size_t frames = raw.buffer_size / (raw.channel_count * sizeof(float));
	return (bigtime_t)(frames * 1000000LL / raw.frame_rate);
}


void
PipeWireInputNode::_SendBuffer(bigtime_t performanceTime)
{
	if (fBufferGroup == NULL || fOutput.destination == media_destination::null)
		return;

	const media_raw_audio_format& raw = fOutput.format.u.raw_audio;
	BBuffer* buffer = fBufferGroup->RequestBuffer(raw.buffer_size,
		_BufferDuration());
	if (buffer == NULL)
		return;

	size_t frames = raw.buffer_size / (raw.channel_count * sizeof(float));
	float* data = (float*)buffer->Data();
	size_t got = fStream != NULL ? fStream->Read(data, frames) : 0;
	if (got < frames) {
		memset(data + got * raw.channel_count, 0,
			(frames - got) * raw.channel_count * sizeof(float));
	}

	media_header* header = buffer->Header();
	header->type = B_MEDIA_RAW_AUDIO;
	header->size_used = raw.buffer_size;
	header->time_source = TimeSource()->ID();
	header->start_time = performanceTime;

	if (!fEnabled
		|| SendBuffer(buffer, fOutput.source, fOutput.destination) != B_OK)
		buffer->Recycle();
}


void
PipeWireInputNode::HandleEvent(const media_timed_event* event,
	bigtime_t lateness, bool realTimeEvent)
{
	switch (event->type) {
		case BTimedEventQueue::B_START:
		{
			if (fRunning)
				break;
			fRunning = true;
			media_timed_event first(event->event_time,
				BTimedEventQueue::B_HANDLE_BUFFER);
			EventQueue()->AddEvent(first);
			break;
		}
		case BTimedEventQueue::B_STOP:
			fRunning = false;
			EventQueue()->FlushEvents(0, BTimedEventQueue::B_ALWAYS, true,
				BTimedEventQueue::B_HANDLE_BUFFER);
			break;
		case BTimedEventQueue::B_HANDLE_BUFFER:
		{
			if (!fRunning)
				break;
			_SendBuffer(event->event_time);
			media_timed_event next(event->event_time + _BufferDuration(),
				BTimedEventQueue::B_HANDLE_BUFFER);
			EventQueue()->AddEvent(next);
			break;
		}
		default:
			break;
	}
}
