/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "MprisService.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <Message.h>
#include <SupportDefs.h>

#include <MPRISInterface.h>


static const char* kMprisServiceName
	= "org.mpris.MediaPlayer2.vos.MediaPlayer";
static const char* kMprisApplicationInterface = "org.mpris.MediaPlayer2";
static const char* kMprisTrackPath
	= "/org/mpris/MediaPlayer2/TrackList/NoTrack";
static const uint32 kMprisWakeEvent = 1;


MprisService::MprisService(const BMessenger& target)
	:
	fTarget(target),
	fRunning(false),
	fStartupStatus(B_ERROR),
	fThread(-1),
	fWakePort(-1),
	fStartupSem(-1),
	fBus(NULL)
{
}


MprisService::~MprisService()
{
	fRunning.store(false);
	if (fThread >= B_OK) {
		status_t result;
		wait_for_thread(fThread, &result);
	}
	if (fWakePort >= B_OK)
		delete_port(fWakePort);
	if (fStartupSem >= B_OK)
		delete_sem(fStartupSem);
}


status_t
MprisService::Start()
{
	if (fThread >= B_OK)
		return B_ALREADY_RUNNING;
	fWakePort = create_port(16, "mediaplayer_mpris");
	if (fWakePort < B_OK)
		return fWakePort;
	fStartupSem = create_sem(0, "mediaplayer_mpris_startup");
	if (fStartupSem < B_OK) {
		delete_port(fWakePort);
		fWakePort = -1;
		return fStartupSem;
	}

	fRunning.store(true);
	fThread = spawn_thread(_Worker, "mediaplayer-mpris",
		B_NORMAL_PRIORITY, this);
	if (fThread < B_OK) {
		fRunning.store(false);
		delete_port(fWakePort);
		fWakePort = -1;
		delete_sem(fStartupSem);
		fStartupSem = -1;
		return fThread;
	}
	status_t status = resume_thread(fThread);
	if (status != B_OK) {
		status_t startStatus = status;
		fRunning.store(false);
		kill_thread(fThread);
		status_t threadStatus;
		wait_for_thread(fThread, &threadStatus);
		fThread = -1;
		delete_port(fWakePort);
		fWakePort = -1;
		delete_sem(fStartupSem);
		fStartupSem = -1;
		return startStatus;
	}
	status = acquire_sem_etc(fStartupSem, 1, B_RELATIVE_TIMEOUT, 5000000);
	if (status != B_OK) {
		fRunning.store(false);
		int32 event = kMprisWakeEvent;
		write_port_etc(fWakePort, kMprisWakeEvent, &event, sizeof(event),
			B_RELATIVE_TIMEOUT, 0);
		status_t threadStatus;
		wait_for_thread(fThread, &threadStatus);
		fThread = -1;
		delete_port(fWakePort);
		fWakePort = -1;
		delete_sem(fStartupSem);
		fStartupSem = -1;
		return status;
	}

	status = fStartupStatus.load();
	if (status != B_OK) {
		fRunning.store(false);
		status_t threadStatus;
		wait_for_thread(fThread, &threadStatus);
		fThread = -1;
		delete_port(fWakePort);
		fWakePort = -1;
		delete_sem(fStartupSem);
		fStartupSem = -1;
	}
	return status;
}


void
MprisService::NotifyPropertiesChanged()
{
	if (fWakePort >= B_OK) {
		int32 event = kMprisWakeEvent;
		status_t status = write_port_etc(fWakePort, kMprisWakeEvent, &event,
			sizeof(event), B_RELATIVE_TIMEOUT, 0);
		if (status != B_OK && status != B_WOULD_BLOCK)
			fprintf(stderr, "MediaPlayer: MPRIS wake failed (%" B_PRId32 ")\n",
				status);
	}
}


int32
MprisService::_Worker(void* data)
{
	return ((MprisService*)data)->_Run();
}


int32
MprisService::_Run()
{
	int result = sd_bus_open_user(&fBus);
	if (result < 0) {
		fprintf(stderr, "MediaPlayer: cannot connect to session bus: %s\n",
			strerror(-result));
		fStartupStatus.store((status_t)result);
		release_sem(fStartupSem);
		return result;
	}

	static const sd_bus_vtable rootVTable[] = {
		SD_BUS_VTABLE_START(0),
		SD_BUS_METHOD("Raise", "", "", _Raise, SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("Quit", "", "", _Quit, SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_PROPERTY("CanQuit", "b", _GetRootProperty, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("CanRaise", "b", _GetRootProperty, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("HasTrackList", "b", _GetRootProperty, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("Identity", "s", _GetRootProperty, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("DesktopEntry", "s", _GetRootProperty, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("SupportedUriSchemes", "as", _GetRootProperty, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("SupportedMimeTypes", "as", _GetRootProperty, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_VTABLE_END
	};
	static const sd_bus_vtable playerVTable[] = {
		SD_BUS_VTABLE_START(0),
		SD_BUS_METHOD("Next", "", "", _Next, SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("Previous", "", "", _Previous,
			SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("Pause", "", "", _Pause, SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("PlayPause", "", "", _PlayPause,
			SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("Stop", "", "", _Stop, SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("Play", "", "", _Play, SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("Seek", "x", "", _Seek, SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("SetPosition", "ox", "", _SetPosition,
			SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_METHOD("OpenUri", "s", "", _OpenUri,
			SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_PROPERTY("PlaybackStatus", "s", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_WRITABLE_PROPERTY("LoopStatus", "s", _GetPlayerProperty,
			_SetPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_WRITABLE_PROPERTY("Rate", "d", _GetPlayerProperty,
			_SetPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_WRITABLE_PROPERTY("Shuffle", "b", _GetPlayerProperty,
			_SetPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_PROPERTY("Metadata", "a{sv}", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_WRITABLE_PROPERTY("Volume", "d", _GetPlayerProperty,
			_SetPlayerProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_PROPERTY("Position", "x", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
		SD_BUS_PROPERTY("MinimumRate", "d", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("MaximumRate", "d", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_PROPERTY("CanGoNext", "b", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_PROPERTY("CanGoPrevious", "b", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_PROPERTY("CanPlay", "b", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_PROPERTY("CanPause", "b", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_PROPERTY("CanSeek", "b", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
		SD_BUS_PROPERTY("CanControl", "b", _GetPlayerProperty, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
		SD_BUS_VTABLE_END
	};

	result = sd_bus_add_object_vtable(fBus, NULL, MPRIS_PATH,
		kMprisApplicationInterface, rootVTable, this);
	if (result >= 0)
		result = sd_bus_add_object_vtable(fBus, NULL, MPRIS_PATH,
			MPRIS_PLAYER_INTERFACE, playerVTable, this);
	if (result < 0) {
		fprintf(stderr, "MediaPlayer: cannot register MPRIS interfaces: %s\n",
			strerror(-result));
		sd_bus_flush_close_unref(fBus);
		fBus = NULL;
		fStartupStatus.store((status_t)result);
		release_sem(fStartupSem);
		return result;
	}

	result = sd_bus_request_name(fBus, kMprisServiceName, 0);
	if (result <= 0) {
		fprintf(stderr, "MediaPlayer: cannot own MPRIS name %s: %s\n",
			kMprisServiceName, result < 0 ? strerror(-result) : "already owned");
		sd_bus_flush_close_unref(fBus);
		fBus = NULL;
		fStartupStatus.store((status_t)(result < 0 ? result : -EEXIST));
		release_sem(fStartupSem);
		return result < 0 ? result : -EEXIST;
	}

	fStartupStatus.store(B_OK);
	release_sem(fStartupSem);
	while (fRunning.load()) {
		int32 code;
		int32 event;
		while (read_port_etc(fWakePort, &code, &event, sizeof(event),
				B_RELATIVE_TIMEOUT, 0) == B_OK) {
			_EmitPropertiesChanged();
		}

		result = sd_bus_process(fBus, NULL);
		if (result < 0) {
			fprintf(stderr, "MediaPlayer: MPRIS bus processing failed: %s\n",
				strerror(-result));
			break;
		}
		if (result > 0)
			continue;
		result = sd_bus_wait(fBus, 100000);
		if (result < 0 && result != -EINTR) {
			fprintf(stderr, "MediaPlayer: MPRIS bus wait failed: %s\n",
				strerror(-result));
			break;
		}
	}

	sd_bus_flush_close_unref(fBus);
	fBus = NULL;
	return 0;
}


status_t
MprisService::_Query(BMessage& reply)
{
	BMessage request('mprq');
	return fTarget.SendMessage(&request, &reply, 5000000LL, 5000000LL);
}


status_t
MprisService::_SendCommand(sd_bus_message* message, const char* action,
	const char* signature)
{
	BMessage request('mprc');
	request.AddString("action", action);
	if (signature != NULL) {
		if (strcmp(signature, "x") == 0) {
			int64 value;
			if (sd_bus_message_read(message, "x", &value) < 0)
				return B_BAD_VALUE;
			request.AddInt64("value", value);
		} else if (strcmp(signature, "ox") == 0) {
			const char* track;
			int64 value;
			if (sd_bus_message_read(message, "ox", &track, &value) < 0)
				return B_BAD_VALUE;
			request.AddString("track", track);
			request.AddInt64("value", value);
		} else if (strcmp(signature, "s") == 0) {
			const char* value;
			if (sd_bus_message_read(message, "s", &value) < 0)
				return B_BAD_VALUE;
			request.AddString(strcmp(action, "OpenUri") == 0 ? "uri" : "loop",
				value);
		} else if (strcmp(signature, "d") == 0) {
			double value;
			if (sd_bus_message_read(message, "d", &value) < 0)
				return B_BAD_VALUE;
			request.AddFloat("value", (float)value);
		}
	}

	status_t status = _SendProperty(request);
	if (status == B_OK
		&& (strcmp(action, "SeekBy") == 0 || strcmp(action, "SeekTo") == 0)) {
		BMessage state;
		if (_Query(state) == B_OK) {
			int64 position = 0;
			if (state.FindInt64("position", &position) == B_OK) {
				int result = sd_bus_emit_signal(fBus, MPRIS_PATH,
					MPRIS_PLAYER_INTERFACE, "Seeked", "x", position);
				if (result < 0)
					fprintf(stderr, "MediaPlayer: MPRIS Seeked signal failed: "
						"%s\n", strerror(-result));
			}
		}
	}
	return status;
}


status_t
MprisService::_SendProperty(BMessage& request)
{
	BMessage reply;
	status_t status = fTarget.SendMessage(&request, &reply, 5000000LL,
		5000000LL);
	if (status == B_OK) {
		status_t replyStatus = reply.FindInt32("result", &status);
		if (replyStatus != B_OK)
			return B_BAD_DATA;
	}
	if (status == B_OK)
		NotifyPropertiesChanged();
	return status;
}


void
MprisService::_EmitPropertiesChanged()
{
	int result = sd_bus_emit_properties_changed(fBus, MPRIS_PATH,
		MPRIS_PLAYER_INTERFACE, "PlaybackStatus", "Metadata", "Volume",
		"Position",
		"CanGoNext", "CanGoPrevious", "CanPlay", "CanPause", "CanSeek", NULL);
	if (result < 0)
		fprintf(stderr, "MediaPlayer: MPRIS property signal failed: %s\n",
			strerror(-result));
}


int
MprisService::_ReplyForCommand(sd_bus_message* message, void* data,
	const char* action, const char* signature)
{
	MprisService* service = static_cast<MprisService*>(data);
	status_t status = service->_SendCommand(message, action, signature);
	if (status != B_OK)
		return sd_bus_reply_method_errorf(message,
			"org.mpris.MediaPlayer2.Player.Error.Failed",
			"MediaPlayer command failed: %s",
			strerror(status < 0 ? -status : status));
	return sd_bus_reply_method_return(message, "");
}


int MprisService::_Raise(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "Raise", NULL);
}
int MprisService::_Quit(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "Quit", NULL);
}
int MprisService::_Play(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "Play", NULL);
}
int MprisService::_Pause(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "Pause", NULL);
}
int MprisService::_PlayPause(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "PlayPause", NULL);
}
int MprisService::_Stop(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "Stop", NULL);
}
int MprisService::_Next(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "Next", NULL);
}
int MprisService::_Previous(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "Previous", NULL);
}
int MprisService::_Seek(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "SeekBy", "x");
}
int MprisService::_SetPosition(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "SeekTo", "ox");
}
int MprisService::_OpenUri(sd_bus_message* m, void* d, sd_bus_error*) {
	return _ReplyForCommand(m, d, "OpenUri", "s");
}


int
MprisService::_GetRootProperty(sd_bus*, const char*, const char*,
	const char* property, sd_bus_message* reply, void*,
	sd_bus_error*)
{
	if (strcmp(property, "CanQuit") == 0)
		return sd_bus_message_append(reply, "b", 1);
	if (strcmp(property, "CanRaise") == 0)
		return sd_bus_message_append(reply, "b", 1);
	if (strcmp(property, "HasTrackList") == 0)
		return sd_bus_message_append(reply, "b", 0);
	if (strcmp(property, "Identity") == 0)
		return sd_bus_message_append(reply, "s", "Vitruvian Media Player");
	if (strcmp(property, "DesktopEntry") == 0)
		return sd_bus_message_append(reply, "s", "vos-MediaPlayer");
	if (strcmp(property, "SupportedUriSchemes") == 0) {
		int result = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY,
			"s");
		if (result >= 0)
			result = sd_bus_message_append(reply, "s", "file");
		if (result >= 0)
			result = sd_bus_message_close_container(reply);
		return result;
	}
	if (strcmp(property, "SupportedMimeTypes") == 0) {
		int result = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY,
			"s");
		return result < 0 ? result : sd_bus_message_close_container(reply);
	}
	return -EINVAL;
}


int
MprisService::_GetPlayerProperty(sd_bus*, const char*, const char*,
	const char* property, sd_bus_message* reply, void* data,
	sd_bus_error*)
{
	MprisService* service = static_cast<MprisService*>(data);
	BMessage state;
	status_t status = service->_Query(state);
	if (status != B_OK)
		return -EIO;

	if (strcmp(property, "PlaybackStatus") == 0) {
		const char* value = state.GetString("status", "Stopped");
		return sd_bus_message_append(reply, "s", value);
	}
	if (strcmp(property, "LoopStatus") == 0)
		return sd_bus_message_append(reply, "s",
			state.GetBool("loop", false) ? "Playlist" : "None");
	if (strcmp(property, "Rate") == 0 || strcmp(property, "MinimumRate") == 0
		|| strcmp(property, "MaximumRate") == 0) {
		return sd_bus_message_append(reply, "d", 1.0);
	}
	if (strcmp(property, "Shuffle") == 0)
		return sd_bus_message_append(reply, "b", 0);
	if (strcmp(property, "Volume") == 0) {
		float volume = 1.0f;
		state.FindFloat("volume", &volume);
		return sd_bus_message_append(reply, "d", (double)volume);
	}
	if (strcmp(property, "Position") == 0) {
		int64 position = 0;
		state.FindInt64("position", &position);
		return sd_bus_message_append(reply, "x", position);
	}
	if (strcmp(property, "Metadata") == 0) {
		const char* title = NULL;
		const char* uri = NULL;
		const char* trackID = NULL;
		int64 duration = 0;
		bool hasTrack = state.GetBool("has_track", false);
		state.FindString("title", &title);
		state.FindString("uri", &uri);
		state.FindString("track_id", &trackID);
		state.FindInt64("duration", &duration);
		int result = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY,
			"{sv}");
		if (result < 0)
			return result;

		result = sd_bus_message_open_container(reply, SD_BUS_TYPE_DICT_ENTRY,
			"sv");
		if (result >= 0)
			result = sd_bus_message_append(reply, "s", "mpris:trackid");
		if (result >= 0)
			result = sd_bus_message_open_container(reply, SD_BUS_TYPE_VARIANT,
				"o");
		if (result >= 0)
			result = sd_bus_message_append(reply, "o",
				hasTrack && trackID != NULL ? trackID : kMprisTrackPath);
		if (result >= 0)
			result = sd_bus_message_close_container(reply);
		if (result >= 0)
			result = sd_bus_message_close_container(reply);
		if (result >= 0 && hasTrack && title != NULL) {
			result = sd_bus_message_open_container(reply,
				SD_BUS_TYPE_DICT_ENTRY, "sv");
			if (result >= 0)
				result = sd_bus_message_append(reply, "s", "xesam:title");
			if (result >= 0)
				result = sd_bus_message_open_container(reply,
					SD_BUS_TYPE_VARIANT, "s");
			if (result >= 0)
				result = sd_bus_message_append(reply, "s", title);
			if (result >= 0)
				result = sd_bus_message_close_container(reply);
			if (result >= 0)
				result = sd_bus_message_close_container(reply);
		}
		if (result >= 0 && hasTrack && uri != NULL) {
			result = sd_bus_message_open_container(reply,
				SD_BUS_TYPE_DICT_ENTRY, "sv");
			if (result >= 0)
				result = sd_bus_message_append(reply, "s", "xesam:url");
			if (result >= 0)
				result = sd_bus_message_open_container(reply,
					SD_BUS_TYPE_VARIANT, "s");
			if (result >= 0)
				result = sd_bus_message_append(reply, "s", uri);
			if (result >= 0)
				result = sd_bus_message_close_container(reply);
			if (result >= 0)
				result = sd_bus_message_close_container(reply);
		}
		if (result >= 0 && hasTrack && duration > 0) {
			result = sd_bus_message_open_container(reply,
				SD_BUS_TYPE_DICT_ENTRY, "sv");
			if (result >= 0)
				result = sd_bus_message_append(reply, "s", "mpris:length");
			if (result >= 0)
				result = sd_bus_message_open_container(reply,
					SD_BUS_TYPE_VARIANT, "x");
			if (result >= 0)
				result = sd_bus_message_append(reply, "x", duration);
			if (result >= 0)
				result = sd_bus_message_close_container(reply);
			if (result >= 0)
				result = sd_bus_message_close_container(reply);
		}
		if (result >= 0)
			result = sd_bus_message_close_container(reply);
		return result;
	}

	bool hasTrack = state.GetBool("has_track", false);
	bool canGo = hasTrack;
	bool canSeek = state.GetBool("can_seek", false);
	if (strcmp(property, "CanGoNext") == 0
		|| strcmp(property, "CanGoPrevious") == 0) {
		const char* name = strcmp(property, "CanGoNext") == 0
			? "can_go_next" : "can_go_previous";
		return sd_bus_message_append(reply, "b", state.GetBool(name, canGo));
	}
	if (strcmp(property, "CanPlay") == 0
		|| strcmp(property, "CanPause") == 0)
		return sd_bus_message_append(reply, "b", hasTrack);
	if (strcmp(property, "CanSeek") == 0)
		return sd_bus_message_append(reply, "b", canSeek);
	if (strcmp(property, "CanControl") == 0)
		return sd_bus_message_append(reply, "b", 1);
	return -EINVAL;
}


int
MprisService::_SetPlayerProperty(sd_bus*, const char*, const char*,
	const char* property, sd_bus_message* value, void* data,
	sd_bus_error* error)
{
	MprisService* service = static_cast<MprisService*>(data);

	if (strcmp(property, "Volume") == 0) {
		double volume;
		if (sd_bus_message_read(value, "d", &volume) < 0
			|| volume < 0.0 || volume > 1.0) {
			return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS,
				"Volume must be between 0 and 1");
		}
		BMessage request('mprc');
		request.AddString("action", "SetVolume");
		request.AddFloat("value", (float)volume);
		status_t status = service->_SendProperty(request);
		if (status != B_OK) {
			return sd_bus_error_setf(error,
				"org.mpris.MediaPlayer2.Player.Error.Failed",
				"MediaPlayer property update failed: %s",
				strerror(status < 0 ? -status : status));
		}
		return 1;
	} else if (strcmp(property, "LoopStatus") == 0) {
		const char* loop;
		if (sd_bus_message_read(value, "s", &loop) < 0
			|| (strcmp(loop, "None") != 0
				&& strcmp(loop, "Playlist") != 0)) {
			return sd_bus_error_setf(error,
				"org.mpris.MediaPlayer2.Player.Error.NotSupported",
				"Only None and Playlist loop modes are supported");
		}
		BMessage request('mprc');
		request.AddString("action", "SetLoop");
		request.AddString("loop", loop);
		status_t status = service->_SendProperty(request);
		if (status != B_OK) {
			return sd_bus_error_setf(error,
				"org.mpris.MediaPlayer2.Player.Error.Failed",
				"MediaPlayer property update failed: %s",
				strerror(status < 0 ? -status : status));
		}
		return 1;
	} else if (strcmp(property, "Rate") == 0) {
		double rate;
		if (sd_bus_message_read(value, "d", &rate) < 0 || rate != 1.0) {
			return sd_bus_error_setf(error,
				"org.mpris.MediaPlayer2.Player.Error.NotSupported",
				"Only playback rate 1.0 is supported");
		}
		return 1;
	} else if (strcmp(property, "Shuffle") == 0) {
		int shuffle;
		if (sd_bus_message_read(value, "b", &shuffle) < 0 || shuffle) {
			return sd_bus_error_setf(error,
				"org.mpris.MediaPlayer2.Player.Error.NotSupported",
				"Shuffle is not supported");
		}
		return 1;
	}
	return -EINVAL;
}
