/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	mixer_monitor_test [rounds] [reconnect|rate]: records the system mixer's
	monitor output while a BSoundPlayer plays a 1 kHz tone, and checks that
	the recording has the tone. Needs a connected sound card (the monitor
	follows its output). Halfway through, "reconnect" connects the mixer
	output to the sound card again, and "rate" switches the sound card to
	another frame rate, as the Media preferences do; the recording must
	follow.
*/


#include <Application.h>
#include <MediaRecorder.h>
#include <MediaRoster.h>
#include <OS.h>
#include <ParameterWeb.h>
#include <SoundPlayer.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


static const float kToneFrequency = 1000;

static double sPhase;

static int64 sFrames;
static int64 sCrossings;
static double sTime;
static double sFirstCrossing;
static double sLastCrossing;
static float sPeak;
static float sLast;
static int32 sBuffers;
static float sFirstRate;
static int32 sChangedBuffers;
static bigtime_t sFirstStart = -1;
static bigtime_t sLastStart;
static media_raw_audio_format sFormat;


static void
play_tone(void* cookie, void* data, size_t size,
	const media_raw_audio_format& format)
{
	float* samples = (float*)data;
	size_t frames = size / sizeof(float) / format.channel_count;
	for (size_t i = 0; i < frames; i++) {
		float value = 0.5 * sin(sPhase);
		sPhase += 2 * M_PI * kToneFrequency / format.frame_rate;
		for (uint32 c = 0; c < format.channel_count; c++)
			*samples++ = value;
	}
}


static void
record(void* cookie, bigtime_t timestamp, void* data, size_t size,
	const media_format& format)
{
	sFormat = format.u.raw_audio;
	if (sFormat.format != media_raw_audio_format::B_AUDIO_FLOAT
		|| sFormat.channel_count == 0)
		return;

	if (sFirstStart < 0)
		sFirstStart = timestamp;
	sLastStart = timestamp;
	sBuffers++;
	if (sBuffers == 1)
		sFirstRate = sFormat.frame_rate;
	else if (sFormat.frame_rate != sFirstRate)
		sChangedBuffers++;

	const float* samples = (const float*)data;
	size_t frames = size / sizeof(float) / sFormat.channel_count;
	for (size_t i = 0; i < frames; i++) {
		float value = samples[i * sFormat.channel_count];
		if (fabsf(value) > sPeak)
			sPeak = fabsf(value);
		if (sLast < 0 && value >= 0) {
			if (sCrossings++ == 0)
				sFirstCrossing = sTime;
			sLastCrossing = sTime;
		}
		sLast = value;
		sTime += 1.0 / sFormat.frame_rate;
	}
	sFrames += frames;
}


/*!	Connects the mixer output to the sound card again, in another format,
	as the Media preferences do.
*/
static status_t
reconnect_output(const media_node& mixer)
{
	BMediaRoster* roster = BMediaRoster::Roster();
	media_node soundCard;
	status_t status = roster->GetAudioOutput(&soundCard);
	if (status != B_OK)
		return status;

	media_input inputs[8];
	int32 count = 0;
	status = roster->GetConnectedInputsFor(soundCard, inputs, 8, &count);
	media_input input;
	for (int32 i = 0; status == B_OK && i < count; i++) {
		if (inputs[i].source.port == mixer.port && inputs[i].source.id == 0)
			input = inputs[i];
	}
	if (input.source == media_source::null) {
		roster->ReleaseNode(soundCard);
		return B_ENTRY_NOT_FOUND;
	}

	media_source source = input.source;
	status = roster->Disconnect(mixer.node, source, soundCard.node,
		input.destination);
	if (status == B_OK) {
		media_format format;
		format.type = B_MEDIA_RAW_AUDIO;
		format.u.raw_audio = media_raw_audio_format::wildcard;
		format.u.raw_audio.frame_rate = input.format.u.raw_audio.frame_rate;
		format.u.raw_audio.channel_count
			= input.format.u.raw_audio.channel_count;
		media_output newOutput;
		media_input newInput;
		status = roster->Connect(source, input.destination, &format,
			&newOutput, &newInput);
	}
	roster->ReleaseNode(soundCard);
	return status;
}


static status_t
find_monitor(const media_node& mixer, media_output* _monitor)
{
	media_output outputs[4];
	int32 count = 0;
	status_t status = BMediaRoster::Roster()->GetAllOutputsFor(mixer, outputs,
		4, &count);
	if (status != B_OK)
		return status;

	for (int32 i = 0; i < count; i++) {
		char format[256];
		string_for_format(outputs[i].format, format, sizeof(format));
		printf("mixer output %" B_PRId32 " \"%s\" -> %" B_PRId32 ":%" B_PRId32
			", %s\n", outputs[i].source.id, outputs[i].name,
			outputs[i].destination.port, outputs[i].destination.id, format);
		if (strcmp(outputs[i].name, "Mixer Monitor") == 0)
			*_monitor = outputs[i];
	}
	return _monitor->source == media_source::null ? B_ENTRY_NOT_FOUND : B_OK;
}


/*!	Sets the sound card's output frame rate parameter to its next choice
	(or back to \a restore), as the Media preferences do.
*/
static status_t
change_rate(int32* _restore)
{
	BMediaRoster* roster = BMediaRoster::Roster();
	media_node soundCard;
	status_t status = roster->GetAudioOutput(&soundCard);
	if (status != B_OK)
		return status;

	BParameterWeb* web = NULL;
	status = roster->GetParameterWebFor(soundCard, &web);
	BDiscreteParameter* rate = NULL;
	for (int32 i = 0; status == B_OK && i < web->CountParameters(); i++) {
		// multi_audio: 2 is the output frequency, 1 the input one, which
		// is the output's, too, if the card has no separate output choice
		BParameter* parameter = web->ParameterAt(i);
		if (parameter->Type() == BParameter::B_DISCRETE_PARAMETER
			&& (parameter->ID() == 2 || (parameter->ID() == 1 && rate == NULL)))
			rate = (BDiscreteParameter*)parameter;
	}
	if (status == B_OK && rate == NULL)
		status = B_ENTRY_NOT_FOUND;

	if (status == B_OK) {
		int32 current = 0;
		size_t size = sizeof(current);
		bigtime_t when;
		rate->GetValue(&current, &size, &when);
		int32 next = *_restore;
		if (next == 0) {
			for (int32 i = 0; i < rate->CountItems(); i++) {
				if (rate->ItemValueAt(i) == current) {
					next = rate->ItemValueAt((i + 1) % rate->CountItems());
					printf("rate \"%s\" -> \"%s\"\n", rate->ItemNameAt(i),
						rate->ItemNameAt((i + 1) % rate->CountItems()));
					break;
				}
			}
		}
		*_restore = current;
		if (next == 0 || next == current)
			status = B_NOT_SUPPORTED;
		else
			status = rate->SetValue(&next, sizeof(next), 0);
	}

	delete web;
	roster->ReleaseNode(soundCard);
	return status;
}


static int
record_once(const media_node& mixer, bigtime_t duration, bool tone,
	const char* change)
{
	sFrames = sCrossings = 0;
	sTime = sFirstCrossing = sLastCrossing = 0;
	sPeak = sLast = 0;
	sBuffers = sChangedBuffers = 0;
	sFirstStart = -1;

	media_output monitor;
	status_t status = find_monitor(mixer, &monitor);
	if (status != B_OK) {
		printf("no monitor output: %s\n", strerror(status));
		return 1;
	}

	BMediaRecorder recorder("mixer monitor test", B_MEDIA_RAW_AUDIO);
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	recorder.SetAcceptedFormat(format);
	recorder.SetHooks(record, NULL, NULL);
	status = recorder.Connect(mixer, &monitor, &format);
	if (status != B_OK) {
		printf("Connect: %s\n", strerror(status));
		return 1;
	}

	// a second recorder cannot connect while the first one is
	BMediaRecorder second("mixer monitor test 2", B_MEDIA_RAW_AUDIO);
	second.SetAcceptedFormat(format);
	status_t secondStatus = second.Connect(mixer, &monitor, &format);
	printf("second recorder: %s\n", strerror(secondStatus));
	if (secondStatus == B_OK)
		second.Disconnect();

	recorder.Start();

	BSoundPlayer* player = NULL;
	if (tone) {
		media_raw_audio_format playFormat = media_raw_audio_format::wildcard;
		playFormat.format = media_raw_audio_format::B_AUDIO_FLOAT;
		playFormat.channel_count = 2;
		playFormat.frame_rate = 48000;
		playFormat.byte_order = B_MEDIA_HOST_ENDIAN;
		player = new BSoundPlayer(&playFormat, "monitor test tone", play_tone);
		if (player->InitCheck() != B_OK) {
			printf("BSoundPlayer: %s\n", strerror(player->InitCheck()));
			return 1;
		}
		player->Start();
		player->SetHasData(true);
	}

	status_t changeStatus = B_OK;
	int32 oldRate = 0;
	if (change != NULL) {
		snooze(duration / 2);
		if (strcmp(change, "rate") == 0)
			changeStatus = change_rate(&oldRate);
		else
			changeStatus = reconnect_output(mixer);
		printf("%s: %s\n", change, strerror(changeStatus));
		snooze(duration / 2);
	} else
		snooze(duration);

	if (player != NULL) {
		player->Stop();
		delete player;
	}
	recorder.Stop();
	recorder.Disconnect();
	if (oldRate != 0)
		change_rate(&oldRate);

	double frequency = sCrossings > 1
		? (sCrossings - 1) / (sLastCrossing - sFirstCrossing) : 0;
	printf("recorded %" B_PRId32 " buffers (%" B_PRId32 " at another rate), %"
		B_PRId64 " frames (%.2f s), last %" B_PRIu32 " ch %g Hz %s, peak "
		"%.3f, tone %.1f Hz, buffer times %.2f s apart\n", sBuffers,
		sChangedBuffers, sFrames, sTime, sFormat.channel_count,
		sFormat.frame_rate,
		sFormat.format == media_raw_audio_format::B_AUDIO_FLOAT
			? "float" : "not float",
		sPeak, frequency, (sLastStart - sFirstStart) / 1000000.0);

	int failures = secondStatus == B_OK ? 1 : 0;
	if (sFormat.format != media_raw_audio_format::B_AUDIO_FLOAT)
		failures++;
	if (tone) {
		if (sPeak < 0.1 || fabs(frequency - kToneFrequency) > 10
			|| sTime < duration / 2000000.0)
			failures++;
	}
	if (change != NULL && changeStatus != B_OK)
		failures++;
	if (oldRate != 0 && sChangedBuffers < 10)
		failures++;
	return failures;
}


int
main(int argc, char** argv)
{
	BApplication app("application/x-vnd.vos-mixer-monitor-test");
	BMediaRoster* roster = BMediaRoster::Roster();
	media_node mixer;
	status_t status = roster->GetAudioMixer(&mixer);
	if (status != B_OK) {
		printf("GetAudioMixer: %s\n", strerror(status));
		return 1;
	}

	int rounds = argc > 1 ? atoi(argv[1]) : 2;
	const char* change = argc > 2 ? argv[2] : NULL;
	int failures = 0;
	for (int i = 0; i < rounds; i++)
		failures += record_once(mixer, 2000000, true, change);

	roster->ReleaseNode(mixer);
	printf("%s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
