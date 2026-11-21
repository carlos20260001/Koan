/* A small film player, to see whether the card's decoder actually plays
 * something.
 *
 * It asks the media kit for pixels and for sound and does nothing clever with
 * either: what makes it interesting is that on this machine the pixels come
 * from the graphics card's video engine, through the nvdec add-on, rather than
 * from the processor. Playing, pausing and seeking are here because a decoder
 * that cannot be interrupted and resumed is not a decoder anyone can use.
 *
 * Sound is the clock. Pictures are shown when the sound has reached them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <Path.h>
#include <Screen.h>
#include <Slider.h>
#include <SoundPlayer.h>
#include <StringView.h>
#include <View.h>
#include <Window.h>

static const uint32 kMsgPlayPause	= 'plpa';
static const uint32 kMsgSeek		= 'seek';
static const uint32 kMsgFrame		= 'fram';
static const uint32 kMsgProgress	= 'prog';
static const uint32 kMsgOpen		= 'open';
static const uint32 kMsgStart		= 'strt';

static const bigtime_t kSeekStep	= 10000000;	/* ten seconds */


static void
formatTime(bigtime_t time, char* out, size_t size)
{
	if (time < 0)
		time = 0;
	int total = (int)(time / 1000000);
	snprintf(out, size, "%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
}


class VideoView : public BView {
public:
	VideoView()
		:
		BView("video", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE),
		fBitmap(NULL)
	{
		SetViewColor(0, 0, 0);
		SetLowColor(0, 0, 0);
	}

	void SetBitmap(BBitmap* bitmap)
	{
		fBitmap = bitmap;
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		if (fBitmap == NULL) {
			FillRect(updateRect, B_SOLID_LOW);
			return;
		}
		/* Keep the shape of the picture, with black at the sides. */
		BRect bounds = Bounds();
		BRect source = fBitmap->Bounds();
		float scale = bounds.Width() / (source.Width() + 1);
		float other = bounds.Height() / (source.Height() + 1);
		if (other < scale)
			scale = other;
		float width = (source.Width() + 1) * scale;
		float height = (source.Height() + 1) * scale;
		BRect target((bounds.Width() - width) / 2, (bounds.Height() - height) / 2, 0, 0);
		target.right = target.left + width - 1;
		target.bottom = target.top + height - 1;
		SetDrawingMode(B_OP_COPY);
		DrawBitmap(fBitmap, source, target, B_FILTER_BITMAP_BILINEAR);
		if (target.left > bounds.left) {
			FillRect(BRect(bounds.left, bounds.top, target.left - 1, bounds.bottom),
				B_SOLID_LOW);
			FillRect(BRect(target.right + 1, bounds.top, bounds.right, bounds.bottom),
				B_SOLID_LOW);
		}
		if (target.top > bounds.top) {
			FillRect(BRect(bounds.left, bounds.top, bounds.right, target.top - 1),
				B_SOLID_LOW);
			FillRect(BRect(bounds.left, target.bottom + 1, bounds.right, bounds.bottom),
				B_SOLID_LOW);
		}
	}

	virtual void AttachedToWindow()
	{
		/* Keys are for the picture, not for whichever button happens to
		 * have been given the focus. */
		MakeFocus(true);
	}

	virtual void MouseDown(BPoint where)
	{
		MakeFocus(true);
	}

	virtual void KeyDown(const char* bytes, int32 numBytes)
	{
		if (numBytes == 1) {
			switch (bytes[0]) {
				case B_SPACE:
					Window()->PostMessage(kMsgPlayPause);
					return;
				case B_LEFT_ARROW:
				case B_RIGHT_ARROW:
				{
					BMessage message(kMsgSeek);
					message.AddInt64("by", bytes[0] == B_LEFT_ARROW
						? -kSeekStep : kSeekStep);
					Window()->PostMessage(&message);
					return;
				}
			}
		}
		BView::KeyDown(bytes, numBytes);
	}

private:
	BBitmap*	fBitmap;
};


/* BSlider knows whether the pointer is on it, but keeps it to itself. */
class SeekBar : public BSlider {
public:
	SeekBar(BMessage* message)
		:
		BSlider("seek", NULL, message, 0, 1000, B_HORIZONTAL)
	{
	}

	bool Dragging() const { return IsTracking(); }
};


class PlayerWindow : public BWindow {
public:
								PlayerWindow(const entry_ref& ref);
	virtual						~PlayerWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

			status_t			InitCheck() const { return fInitStatus; }

private:
	static	int32				_PlayThread(void* data);
			void				_Play();
	static	int32				_SoundThread(void* data);
			void				_FeedSound();
			bigtime_t			_Now();
			void				_SetPlaying(bool playing);
			void				_DoSeek(bigtime_t to);
	static	void				_AudioCallback(void* cookie, void* buffer,
									size_t size, const media_raw_audio_format&);
			void				_FillAudio(void* buffer, size_t size);

			BMediaFile*			fFile;
			BMediaTrack*		fVideo;
			BMediaTrack*		fAudio;
			BSoundPlayer*		fSound;

			VideoView*			fView;
			BButton*			fPlayPause;
			SeekBar*			fSeekBar;
			BStringView*		fTimeLabel;

			BBitmap*			fBitmaps[2];
			int					fNextBitmap;

			thread_id			fPlayThread;
			thread_id			fSoundThread;

			/* Sound is decoded ahead into this, so that the thread the
			 * sound card pulls from never has to wait for a decoder. */
			uint8*				fRing;
			size_t				fRingSize;
			size_t				fRingRead;
			size_t				fRingWrite;
			sem_id				fRingLock;
			/* One reader serves both tracks and does not lock itself, so
			 * every call into the media kit goes through this. */
			sem_id				fMediaLock;
			bool				fSoundEnded;
			bool				fSoundStarted;
			uint8*				fStaging;
			size_t				fStagingSize;
			bool				fRunning;
			bool				fPlaying;
			bool				fEndOfFilm;

			bigtime_t			fDuration;
			bigtime_t			fPosition;
			/* A film's own clock need not start at zero - a piece cut out
			 * of a longer one often does not - so the first picture says
			 * where the beginning is. */
			bigtime_t			fOrigin;
			bool				fHaveOrigin;
			bool				fNeedClock;

			/* The sound clock: how much sound has been handed to the card. */
			int64				fAudioFrames;
			double				fAudioRate;
			size_t				fAudioFrameSize;
			bigtime_t			fAudioStart;
			bigtime_t			fSystemStart;
			bigtime_t			fSystemStartPosition;

			bigtime_t			fSeekTo;
			bool				fSeekWanted;
			sem_id				fSeekDone;

			status_t			fInitStatus;
			int					fWidth;
			int					fHeight;
			size_t				fRowBytes;
};


PlayerWindow::PlayerWindow(const entry_ref& ref)
	:
	BWindow(BRect(80, 80, 80 + 960, 80 + 600), "NVPlay", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fFile(NULL),
	fVideo(NULL),
	fAudio(NULL),
	fSound(NULL),
	fNextBitmap(0),
	fPlayThread(-1),
	fSoundThread(-1),
	fRing(NULL),
	fRingSize(0),
	fRingRead(0),
	fRingWrite(0),
	fRingLock(-1),
	fMediaLock(-1),
	fSoundEnded(false),
	fSoundStarted(false),
	fStaging(NULL),
	fStagingSize(0),
	fRunning(true),
	fPlaying(false),
	fEndOfFilm(false),
	fDuration(0),
	fPosition(0),
	fOrigin(0),
	fHaveOrigin(false),
	fNeedClock(true),
	fAudioFrames(0),
	fAudioRate(0),
	fAudioFrameSize(0),
	fAudioStart(0),
	fSystemStart(0),
	fSystemStartPosition(0),
	fSeekTo(0),
	fSeekWanted(false),
	fInitStatus(B_NO_INIT),
	fWidth(0),
	fHeight(0),
	fRowBytes(0)
{
	fBitmaps[0] = NULL;
	fBitmaps[1] = NULL;
	fSeekDone = create_sem(0, "nvplay seek");
	fMediaLock = create_sem(1, "nvplay media");

	fFile = new BMediaFile(&ref);
	if (fFile->InitCheck() != B_OK) {
		fInitStatus = fFile->InitCheck();
		return;
	}
	for (int i = 0; i < fFile->CountTracks(); i++) {
		BMediaTrack* track = fFile->TrackAt(i);
		media_format format;
		if (track->EncodedFormat(&format) != B_OK) {
			fFile->ReleaseTrack(track);
			continue;
		}
		if (format.IsVideo() && fVideo == NULL)
			fVideo = track;
		else if (format.IsAudio() && fAudio == NULL)
			fAudio = track;
		else
			fFile->ReleaseTrack(track);
	}
	if (fVideo == NULL) {
		fInitStatus = B_MEDIA_BAD_FORMAT;
		return;
	}

	media_format format;
	memset(&format, 0, sizeof(format));
	format.type = B_MEDIA_RAW_VIDEO;
	format.u.raw_video.display.format = B_RGB32;
	if (fVideo->DecodedFormat(&format) != B_OK) {
		fInitStatus = B_MEDIA_BAD_FORMAT;
		return;
	}
	fWidth = format.u.raw_video.display.line_width;
	fHeight = format.u.raw_video.display.line_count;
	fRowBytes = format.u.raw_video.display.bytes_per_row;
	fDuration = fVideo->Duration();

	media_codec_info codec;
	BString title("NVPlay");
	if (fVideo->GetCodecInfo(&codec) == B_OK) {
		BPath path(&ref);
		title.SetToFormat("%s - %s", path.Leaf(), codec.pretty_name);
	}
	SetTitle(title.String());

	for (int i = 0; i < 2; i++) {
		fBitmaps[i] = new BBitmap(BRect(0, 0, fWidth - 1, fHeight - 1), 0,
			B_RGB32, fRowBytes);
		if (fBitmaps[i]->InitCheck() != B_OK) {
			fInitStatus = fBitmaps[i]->InitCheck();
			return;
		}
		memset(fBitmaps[i]->Bits(), 0, fBitmaps[i]->BitsLength());
	}

	if (fAudio != NULL && getenv("NVPLAY_NO_SOUND") != NULL) {
		/* For telling a fault in the picture from one in the sound. */
		fFile->ReleaseTrack(fAudio);
		fAudio = NULL;
	}
	if (fAudio != NULL) {
		media_format audioFormat;
		memset(&audioFormat, 0, sizeof(audioFormat));
		audioFormat.type = B_MEDIA_RAW_AUDIO;
		audioFormat.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
		audioFormat.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
		audioFormat.u.raw_audio.buffer_size = 16384;
		if (fAudio->DecodedFormat(&audioFormat) == B_OK) {
			fAudioRate = audioFormat.u.raw_audio.frame_rate;
			fAudioFrameSize = (audioFormat.u.raw_audio.format & 0xf)
				* audioFormat.u.raw_audio.channel_count;
			/* A second of sound to decode ahead into, and a staging
			 * buffer of whatever size the decoder settled on - it writes
			 * that much in one call and is not told where the end is. */
			fRingSize = (size_t)(fAudioRate * fAudioFrameSize) + fAudioFrameSize;
			fRing = (uint8*)malloc(fRingSize);
			fStagingSize = audioFormat.u.raw_audio.buffer_size;
			if (fStagingSize < 64 * 1024)
				fStagingSize = 64 * 1024;
			fStagingSize *= 4;
			fStaging = (uint8*)malloc(fStagingSize);
			fRingLock = create_sem(1, "nvplay sound");
			media_raw_audio_format playFormat = audioFormat.u.raw_audio;
			playFormat.buffer_size = 4096 * fAudioFrameSize;
			fSound = new BSoundPlayer(&playFormat, "nvplay", _AudioCallback,
				NULL, this);
			if (fSound->InitCheck() != B_OK || fRing == NULL || fStaging == NULL) {
				delete fSound;
				fSound = NULL;
			}
		} else {
			fFile->ReleaseTrack(fAudio);
			fAudio = NULL;
		}
	}

	fView = new VideoView();
	fPlayPause = new BButton("playpause", "Play", new BMessage(kMsgPlayPause));
	fSeekBar = new SeekBar(new BMessage(kMsgSeek));
	fSeekBar->SetModificationMessage(new BMessage(kMsgSeek));
	fTimeLabel = new BStringView("time", "0:00:00 / 0:00:00");

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fView)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_INSETS)
			.Add(fPlayPause)
			.Add(fSeekBar)
			.Add(fTimeLabel)
		.End();

	char whole[32];
	formatTime(fDuration, whole, sizeof(whole));
	BString label;
	label.SetToFormat("0:00:00 / %s", whole);
	fTimeLabel->SetText(label.String());

	fPlayThread = spawn_thread(_PlayThread, "nvplay", B_DISPLAY_PRIORITY, this);
	if (fPlayThread >= 0)
		resume_thread(fPlayThread);
	if (fSound != NULL) {
		fSoundThread = spawn_thread(_SoundThread, "nvplay sound",
			B_DISPLAY_PRIORITY, this);
		if (fSoundThread >= 0)
			resume_thread(fSoundThread);
	}
	fInitStatus = B_OK;
	PostMessage(kMsgStart);
}


PlayerWindow::~PlayerWindow()
{
	fRunning = false;
	if (fSound != NULL) {
		fSound->Stop();
		delete fSound;
	}
	if (fPlayThread >= 0) {
		status_t ignored;
		wait_for_thread(fPlayThread, &ignored);
	}
	if (fSoundThread >= 0) {
		status_t ignored;
		wait_for_thread(fSoundThread, &ignored);
	}
	free(fRing);
	free(fStaging);
	if (fRingLock >= 0)
		delete_sem(fRingLock);
	if (fMediaLock >= 0)
		delete_sem(fMediaLock);
	delete fBitmaps[0];
	delete fBitmaps[1];
	delete fFile;
	delete_sem(fSeekDone);
}


bool
PlayerWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


/* Where playing has got to, in the film's own time. */
bigtime_t
PlayerWindow::_Now()
{
	if (fSound != NULL && fAudioRate > 0) {
		bigtime_t played = (bigtime_t)(fAudioFrames * 1000000.0 / fAudioRate);
		return fAudioStart + played - fSound->Latency();
	}
	if (!fPlaying)
		return fSystemStartPosition;
	return fSystemStartPosition + (system_time() - fSystemStart);
}


void
PlayerWindow::_AudioCallback(void* cookie, void* buffer, size_t size,
	const media_raw_audio_format&)
{
	((PlayerWindow*)cookie)->_FillAudio(buffer, size);
}


/* Take what has been decoded ahead. Anything missing is silence, and is not
 * counted as played, so the clock waits rather than running away. */
void
PlayerWindow::_FillAudio(void* buffer, size_t size)
{
	memset(buffer, 0, size);
	if (fRing == NULL || !fPlaying || fSeekWanted)
		return;
	acquire_sem(fRingLock);
	size_t have = fRingWrite >= fRingRead
		? fRingWrite - fRingRead : fRingSize - fRingRead + fRingWrite;
	size_t take = have < size ? have : size;
	size_t at = 0;
	while (at < take) {
		size_t run = fRingSize - fRingRead;
		if (run > take - at)
			run = take - at;
		memcpy((uint8*)buffer + at, fRing + fRingRead, run);
		fRingRead = (fRingRead + run) % fRingSize;
		at += run;
	}
	release_sem(fRingLock);
	if (fAudioFrameSize > 0)
		atomic_add64(&fAudioFrames, take / fAudioFrameSize);
}


int32
PlayerWindow::_SoundThread(void* data)
{
	((PlayerWindow*)data)->_FeedSound();
	return 0;
}


void
PlayerWindow::_FeedSound()
{
	// The first sound reads are traced to stderr when NVPLAY_TRACE is set;
	// the committed source used these two without declaring them.
	const bool trace = getenv("NVPLAY_TRACE") != NULL;
	int rounds = 0;

	while (fRunning) {
		if (fAudio == NULL || fRing == NULL || !fPlaying || fSeekWanted
			|| fSoundEnded) {
			snooze(10000);
			continue;
		}
		acquire_sem(fRingLock);
		size_t used = fRingWrite >= fRingRead
			? fRingWrite - fRingRead : fRingSize - fRingRead + fRingWrite;
		size_t free = fRingSize - used - 1;
		release_sem(fRingLock);
		if (free < fStagingSize / 2) {
			snooze(5000);
			continue;
		}

		int64 frames = 0;
		acquire_sem(fMediaLock);
		status_t status = fAudio->ReadFrames(fStaging, &frames);
		release_sem(fMediaLock);
		if (status == B_LAST_BUFFER_ERROR) {
			fSoundEnded = true;
			snooze(50000);
			continue;
		}
		if (trace && rounds < 6) {
			fprintf(stderr, "sound: read %lld frames, %s\n", frames,
				strerror(status));
			fflush(stderr);
			rounds++;
		}
		if (status != B_OK || frames <= 0) {
			snooze(20000);
			continue;
		}
		size_t bytes = (size_t)frames * fAudioFrameSize;
		if (bytes > fStagingSize)
			bytes = fStagingSize;
		if (bytes > free)
			bytes = free;
		acquire_sem(fRingLock);
		size_t at = 0;
		while (at < bytes) {
			size_t run = fRingSize - fRingWrite;
			if (run > bytes - at)
				run = bytes - at;
			memcpy(fRing + fRingWrite, fStaging + at, run);
			fRingWrite = (fRingWrite + run) % fRingSize;
			at += run;
		}
		release_sem(fRingLock);
	}
}


void
PlayerWindow::_SetPlaying(bool playing)
{
	if (playing == fPlaying)
		return;
	if (!playing && fSound != NULL)
		fPosition = _Now();
	fPlaying = playing;
	if (playing) {
		fNeedClock = true;
		fSystemStart = system_time();
		fSystemStartPosition = fPosition;
		if (fSound != NULL) {
			fAudioStart = fPosition;
			fAudioFrames = 0;
			/* Started once and left running: stopping it does not
			 * reliably start again, and starting one that is already
			 * running stops the card asking for sound at all. While
			 * paused it is handed silence, which is what stops the
			 * clock. */
			if (!fSoundStarted) {
				fSound->SetHasData(true);
				fSound->Start();
				fSoundStarted = true;
			}
		}
	}
	fPlayPause->SetLabel(playing ? "Pause" : "Play");
}


void
PlayerWindow::_DoSeek(bigtime_t to)
{
	if (to < fOrigin)
		to = fOrigin;
	if (fDuration > 0 && to > fOrigin + fDuration)
		to = fOrigin + fDuration;
	fSeekTo = to;
	fSeekWanted = true;
	/* Let the playing thread do it, so that nothing is decoding meanwhile. */
	acquire_sem_etc(fSeekDone, 1, B_RELATIVE_TIMEOUT, 2000000);
}


int32
PlayerWindow::_PlayThread(void* data)
{
	((PlayerWindow*)data)->_Play();
	return 0;
}


void
PlayerWindow::_Play()
{
	bigtime_t lastProgress = 0;
	while (fRunning) {
		if (fSeekWanted) {
			bigtime_t to = fSeekTo;
			acquire_sem(fMediaLock);
			if (fVideo != NULL)
				fVideo->SeekToTime(&to, B_MEDIA_SEEK_CLOSEST_BACKWARD);
			if (fAudio != NULL) {
				bigtime_t audioTo = fSeekTo;
				fAudio->SeekToTime(&audioTo, B_MEDIA_SEEK_CLOSEST_BACKWARD);
			}
			release_sem(fMediaLock);
			fSoundEnded = false;
			if (fRing != NULL) {
				acquire_sem(fRingLock);
				fRingRead = 0;
				fRingWrite = 0;
				release_sem(fRingLock);
			}
			fPosition = fSeekTo;
			fNeedClock = true;
			fSystemStart = system_time();
			fSystemStartPosition = fPosition;
			fAudioStart = fPosition;
			fAudioFrames = 0;
			fEndOfFilm = false;
			fSeekWanted = false;
			release_sem(fSeekDone);
			continue;
		}
		if (!fPlaying || fEndOfFilm) {
			snooze(20000);
			continue;
		}

		BBitmap* bitmap = fBitmaps[fNextBitmap];
		int64 count = 0;
		media_header header;
		acquire_sem(fMediaLock);
		status_t status = fVideo->ReadFrames(bitmap->Bits(), &count, &header);
		release_sem(fMediaLock);
		if (status != B_OK) {
			fEndOfFilm = true;
			PostMessage(kMsgPlayPause);
			continue;
		}
		fPosition = header.start_time;
		if (!fHaveOrigin) {
			fOrigin = header.start_time;
			fHaveOrigin = true;
		}
		if (fNeedClock) {
			/* Start the clock at the first picture actually shown, rather
			 * than at whatever the film calls zero. */
			fSystemStart = system_time();
			fSystemStartPosition = header.start_time;
			fAudioStart = header.start_time;
			fAudioFrames = 0;
			fNeedClock = false;
		}

		/* Wait until the sound has reached this picture. A picture that is
		 * already late is shown at once rather than dropped, because this is
		 * for looking at rather than for keeping time. */
		bigtime_t waitedSince = system_time();
		for (;;) {
			if (!fRunning || fSeekWanted || !fPlaying)
				break;
			bigtime_t now = _Now();
			bigtime_t ahead = header.start_time - now;
			if (ahead <= 0)
				break;
			/* Never wait for a clock that has stopped moving. */
			if (system_time() - waitedSince > 1000000)
				break;
			snooze(ahead > 20000 ? 20000 : ahead);
		}
		if (fSeekWanted || !fRunning)
			continue;

		BMessage message(kMsgFrame);
		message.AddInt32("which", fNextBitmap);
		PostMessage(&message);
		fNextBitmap = 1 - fNextBitmap;

		if (system_time() - lastProgress > 200000) {
			lastProgress = system_time();
			PostMessage(kMsgProgress);
			if (getenv("NVPLAY_VERBOSE") != NULL) {
				printf("showing %.2f s, sound at %.2f s\n",
					header.start_time / 1000000.0, _Now() / 1000000.0);
				fflush(stdout);
			}
		}
	}
}


void
PlayerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStart:
			_SetPlaying(true);
			break;

		case kMsgPlayPause:
			if (fEndOfFilm && !fPlaying) {
				_DoSeek(fOrigin);
				fEndOfFilm = false;
			}
			_SetPlaying(!fPlaying);
			break;

		case kMsgFrame:
		{
			int32 which = 0;
			if (message->FindInt32("which", &which) == B_OK)
				fView->SetBitmap(fBitmaps[which]);
			break;
		}

		case kMsgProgress:
		{
			bigtime_t now = _Now() - fOrigin;
			char at[32], whole[32];
			formatTime(now, at, sizeof(at));
			formatTime(fDuration, whole, sizeof(whole));
			BString label;
			label.SetToFormat("%s / %s", at, whole);
			fTimeLabel->SetText(label.String());
			if (fDuration > 0 && !fSeekBar->Dragging())
				fSeekBar->SetValue((int32)(now * 1000 / fDuration));
			break;
		}

		case kMsgSeek:
		{
			int64 by = 0;
			if (message->FindInt64("by", &by) == B_OK) {
				_DoSeek(_Now() + by);
				break;
			}
			if (fDuration > 0)
				_DoSeek(fOrigin + (bigtime_t)fSeekBar->Value() * fDuration / 1000);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


class PlayerApplication : public BApplication {
public:
	PlayerApplication()
		:
		BApplication("application/x-vnd.vos-NVPlay"),
		fOpened(false)
	{
	}

	virtual void RefsReceived(BMessage* message)
	{
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++)
			_Open(ref);
	}

	virtual void ReadyToRun()
	{
		if (!fOpened) {
			printf("usage: NVPlay <film>\n");
			PostMessage(B_QUIT_REQUESTED);
		}
	}

	virtual void ArgvReceived(int32 argc, char** argv)
	{
		for (int32 i = 1; i < argc; i++) {
			entry_ref ref;
			if (get_ref_for_path(argv[i], &ref) == B_OK)
				_Open(ref);
			else
				printf("cannot find %s\n", argv[i]);
		}
	}

private:
	void _Open(const entry_ref& ref)
	{
		PlayerWindow* window = new PlayerWindow(ref);
		if (window->InitCheck() != B_OK) {
			BString text;
			text.SetToFormat("Cannot play %s: %s", ref.name,
				strerror(window->InitCheck()));
			window->Lock();
			window->Quit();
			BAlert* alert = new BAlert("NVPlay", text.String(), "Oh");
			alert->Go(NULL);
			return;
		}
		window->Show();
		fOpened = true;
	}

	bool	fOpened;
};


int
main()
{
	PlayerApplication application;
	application.Run();
	return 0;
}
