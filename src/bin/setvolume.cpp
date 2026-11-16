/*
 * Copyright 2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <Application.h>
#include <MediaRoster.h>
#include <ParameterWeb.h>

#include <OnScreenDisplay.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


extern const char *__progname;
static const char *sProgramName = __progname;


// without parameters to get the actual mute status
int
handleMute(BParameter *muteParameter, int mute = 2, bool toggle = false) {
	int32 muted = 0;
	bigtime_t lastChange = 0;
	size_t size = sizeof(int32);
	muteParameter->GetValue(&muted, &size, &lastChange);
	if (toggle)
		mute = 1 - muted;
	if (muted != mute && mute < 2)
		muteParameter->SetValue(&mute, sizeof(int32), system_time());
	return muted;
}


// shows the volume on screen, as the volume keys change it
static void
showVolume(BContinuousParameter *gain, BParameter *muteParameter)
{
	float volume;
	bigtime_t when;
	size_t size = sizeof(volume);
	if (gain->GetValue(&volume, &size, &when) != B_OK)
		return;

	int32 muted = 0;
	size = sizeof(muted);
	if (muteParameter != NULL)
		muteParameter->GetValue(&muted, &size, &when);

	float range = gain->MaxValue() - gain->MinValue();

	BMessage message(kMsgShowLevel);
	message.AddInt32("kind", B_OSD_VOLUME);
	message.AddFloat("level",
		range > 0 ? (volume - gain->MinValue()) / range : 0);
	message.AddBool("muted", muted != 0);
	message.AddFloat("gain", volume);
	show_osd_level(message);
}


int
main(int argc, char **argv)
{
	BApplication app("application/x-vnd.vos.setvolume");

	BMediaRoster *roster = BMediaRoster::Roster();
	if (roster == NULL) {
		fprintf(stderr, "%s: media roster not available\n", sProgramName);
		return 1;
	}

	media_node mixer;
	status_t status = roster->GetAudioMixer(&mixer);
	if (status != B_OK) {
		fprintf(stderr, "%s: cannot get audio mixer: %s\n", sProgramName, strerror(status));
		return 1;
	}

	BParameterWeb *web;
	status = roster->GetParameterWebFor(mixer, &web);

	roster->ReleaseNode(mixer);
		// the web is all we need :-)

	if (status != B_OK) {
		fprintf(stderr, "%s: cannot get parameter web for audio mixer: %s\n",
			sProgramName, strerror(status));
		return 1;
	}

	BContinuousParameter *gain = NULL;
	BParameter *mute = NULL;

	BParameter *parameter;
	for (int32 index = 0; (parameter = web->ParameterAt(index)) != NULL; index++) {
		// assume the mute preceeding master gain control
		if (!strcmp(parameter->Kind(), B_MUTE))
			mute = parameter;

		if (!strcmp(parameter->Kind(), B_MASTER_GAIN)) {
			gain = dynamic_cast<BContinuousParameter *>(parameter);
			break;
		}
	}

	if (gain == NULL) {
		fprintf(stderr, "%s: could not found master gain!\n", sProgramName);
		delete web;
		return 1;
	}

	float volume = 0.0;
	bool stepped = false;

	if (argc > 1) {
		if (strcmp(argv[1], "-m") == 0 || strcmp(argv[1], "--mute") == 0) {
			handleMute(mute, 1);
			showVolume(gain, mute);
			printf("Muted\n");
			return 0;
		} else if (strcmp(argv[1], "-u") == 0 || strcmp(argv[1], "--unmute") == 0) {
			handleMute(mute, 0);
			showVolume(gain, mute);
			printf("Unmuted\n");
			return 0;
		} else if (strcmp(argv[1], "-t") == 0 || strcmp(argv[1], "--togglemute") == 0) {
			int32 muted = handleMute(mute, 0, true);
			showVolume(gain, mute);
			printf("%s\n", muted ? "Unmuted" : "Muted");
				// handleMute() returns the state before the toggle
			return 0;
		} else {
			if (strcmp(argv[1], "-i") == 0 || strcmp(argv[1], "-d") == 0
				|| strcmp(argv[1], "--increase") == 0 || strcmp(argv[1], "--decrease") == 0) {
				bigtime_t when;
				size_t size = sizeof(volume);
				gain->GetValue(&volume, &size, &when);
				stepped = true;
				size_t step = 3;
				if (argc > 2)
					step = atoi(argv[2]);
				if (strcmp(argv[1], "-i") == 0 || strcmp(argv[1], "--increase") == 0) {
					// make sure it's unmuted
					int muted = handleMute(mute, 0);
					if (muted == 0 || volume <= gain->MinValue())
						volume += step;
				} else if (handleMute(mute) == 0) {
					// not already muted
					volume -= step;
					// make sure it's muted
					if (volume <= gain->MinValue())
						handleMute(mute, 1);
				}
			} else {
				char *end;
				volume = strtod(argv[1], &end);
				if (end == argv[1]) {
					fprintf(stderr,
						"Usage: %s <volume> | [options]\n"
						"Sets the system volume to the specified value in dB.\n"
						"Alternatively there are these options:\n"
						"  -m  --mute\n"
						"  -u  --unmute\n"
						"  -t  --togglemute\ttoggles muting\n"
						"  -i  --increase x\tincreases volume by x dB\n"
						"  -d  --decrease x\tdecreases volume by x dB\n"
						"\t\t\tx defaults to 3 if not supplied\n"
						"The options show the new volume on screen, too.\n",
						sProgramName);
					exit(1);
				}
			}

			// make sure our parameter is in range
			if (volume > gain->MaxValue())
				volume = gain->MaxValue();
			else if (volume < gain->MinValue())
				volume = gain->MinValue();

			gain->SetValue(&volume, sizeof(volume), system_time());
			if (stepped)
				showVolume(gain, mute);
		}
	}
	bigtime_t when;
	size_t size = sizeof(volume);
	gain->GetValue(&volume, &size, &when);

	printf("Current volume: %g (min = %g, max = %g)\n", volume, gain->MinValue(), gain->MaxValue());

	delete web;
	return 0;
}

