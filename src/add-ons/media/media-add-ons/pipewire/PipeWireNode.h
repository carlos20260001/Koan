/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef VOS_PIPEWIRE_MEDIA_NODE_H
#define VOS_PIPEWIRE_MEDIA_NODE_H


#include <BufferConsumer.h>
#include <BufferProducer.h>
#include <MediaEventLooper.h>
#include <String.h>


class BBufferGroup;
class PipeWireAddOn;
class PipeWireStream;


/*!	A physical audio output: what its producer (the system mixer) sends is
	played by a PipeWire stream to the node's sink, or to PipeWire's
	default sink. The stream exists while the node is connected.
	TODO(linux backend): the node is not a time source; the system time
	source drives the mixer, and the ring buffer takes up the drift
	between it and the sound card's clock (dropping or padding audio). */
class PipeWireOutputNode : public BBufferConsumer, public BMediaEventLooper {
public:
								PipeWireOutputNode(PipeWireAddOn* addOn,
									int32 flavorID, const char* name,
									const char* target);
	virtual						~PipeWireOutputNode();

	// BMediaNode
	virtual	BMediaAddOn*		AddOn(int32* _internalID) const;
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

protected:
	virtual	void				NodeRegistered();

	// BBufferConsumer
	virtual	status_t			AcceptFormat(
									const media_destination& destination,
									media_format* format);
	virtual	status_t			GetNextInput(int32* cookie,
									media_input* _input);
	virtual	void				DisposeInputCookie(int32 cookie);
	virtual	void				BufferReceived(BBuffer* buffer);
	virtual	void				ProducerDataStatus(
									const media_destination& destination,
									int32 status, bigtime_t performanceTime);
	virtual	status_t			GetLatencyFor(
									const media_destination& destination,
									bigtime_t* _latency,
									media_node_id* _timeSource);
	virtual	status_t			Connected(const media_source& producer,
									const media_destination& destination,
									const media_format& format,
									media_input* _input);
	virtual	void				Disconnected(const media_source& producer,
									const media_destination& destination);
	virtual	status_t			FormatChanged(const media_source& producer,
									const media_destination& consumer,
									int32 changeTag,
									const media_format& format);

	// BMediaEventLooper
	virtual	void				HandleEvent(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);

private:
			void				_GetFormat(media_format& format) const;

			PipeWireAddOn*		fAddOn;
			int32				fFlavorID;
			BString				fTarget;
			media_input			fInput;
			PipeWireStream*		fStream;
};


/*!	A physical audio input: a PipeWire capture stream from the node's
	source, or PipeWire's default source, sent on in buffers of the
	connected format while the node runs. */
class PipeWireInputNode : public BBufferProducer, public BMediaEventLooper {
public:
								PipeWireInputNode(PipeWireAddOn* addOn,
									int32 flavorID, const char* name,
									const char* target);
	virtual						~PipeWireInputNode();

	// BMediaNode
	virtual	BMediaAddOn*		AddOn(int32* _internalID) const;
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

protected:
	virtual	void				NodeRegistered();

	// BBufferProducer
	virtual	status_t			FormatSuggestionRequested(media_type type,
									int32 quality, media_format* format);
	virtual	status_t			FormatProposal(const media_source& output,
									media_format* format);
	virtual	status_t			FormatChangeRequested(
									const media_source& source,
									const media_destination& destination,
									media_format* format,
									int32* _deprecated_);
	virtual	status_t			GetNextOutput(int32* cookie,
									media_output* _output);
	virtual	status_t			DisposeOutputCookie(int32 cookie);
	virtual	status_t			SetBufferGroup(const media_source& source,
									BBufferGroup* group);
	virtual	status_t			PrepareToConnect(const media_source& what,
									const media_destination& where,
									media_format* format,
									media_source* _source, char* _name);
	virtual	void				Connect(status_t error,
									const media_source& source,
									const media_destination& destination,
									const media_format& format,
									char* ioName);
	virtual	void				Disconnect(const media_source& what,
									const media_destination& where);
	virtual	void				LateNoticeReceived(const media_source& what,
									bigtime_t howMuch,
									bigtime_t performanceTime);
	virtual	void				EnableOutput(const media_source& what,
									bool enabled, int32* _deprecated_);

	// BMediaEventLooper
	virtual	void				HandleEvent(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);

private:
			void				_GetFormat(media_format& format) const;
			bigtime_t			_BufferDuration() const;
			void				_SendBuffer(bigtime_t performanceTime);

			PipeWireAddOn*		fAddOn;
			int32				fFlavorID;
			BString				fTarget;
			media_output		fOutput;
			PipeWireStream*		fStream;
			BBufferGroup*		fBufferGroup;
			bool				fOwnBufferGroup;
			bool				fEnabled;
			bool				fRunning;
			bigtime_t			fLatency;
};


#endif	// VOS_PIPEWIRE_MEDIA_NODE_H
