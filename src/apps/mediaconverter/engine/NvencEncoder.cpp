/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "NvencEncoder.h"

#include <Autolock.h>
#include <File.h>
#include <FindDirectory.h>
#include <Locker.h>
#include <Message.h>
#include <Path.h>
#include <image.h>

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <sys/prctl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>


static const char* kTestCacheName = "MediaConverter NVENC test";
static const uint32 kCodecBits[] = {
	NVENC_CODEC_H264, NVENC_CODEC_HEVC, NVENC_CODEC_HEVC_10, NVENC_CODEC_AV1,
	NVENC_CODEC_AV1_10
};
static const int kCodecSlots = sizeof(kCodecBits) / sizeof(kCodecBits[0]);

enum {
	kUntested = 0,
	kWorks = 1,
	kFails = -1,
};

struct TestResult {
	int		state;
	uint32	variant;
	BString	reason;
};

static BLocker sLock("nvenc add-on");
static BLocker sTestLock("nvenc self-test");
static bool sTried;
static const nvenc_api* sApi;
static uint32 sCodecs;
static BString sReason;
static BString sName;
static BString sCacheKey;
static TestResult sTests[kCodecSlots];


static int
codec_slot(uint32 bit)
{
	for (int i = 0; i < kCodecSlots; i++) {
		if (kCodecBits[i] == bit)
			return i;
	}
	return -1;
}


/*!	Test results are kept per card and add-on build, so the test runs once
	and again only when either changes. */
static void
load_test_results()
{
	if (getenv("MEDIACONVERTER_NVENC_RETEST") != NULL)
		return;
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append(kTestCacheName) != B_OK)
		return;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage archive;
	if (file.InitCheck() != B_OK || archive.Unflatten(&file) != B_OK)
		return;
	if (sCacheKey != archive.GetString("key", ""))
		return;
	for (int i = 0; i < kCodecSlots; i++) {
		BMessage result;
		if (archive.FindMessage("result", i, &result) != B_OK)
			break;
		int slot = codec_slot(result.GetUInt32("codec", 0));
		if (slot < 0)
			continue;
		sTests[slot].state = result.GetInt32("state", kUntested);
		sTests[slot].variant = result.GetUInt32("variant", 0);
		sTests[slot].reason = result.GetString("reason", "");
	}
}


static void
save_test_results()
{
	BMessage archive;
	archive.AddString("key", sCacheKey);
	for (int i = 0; i < kCodecSlots; i++) {
		if (sTests[i].state == kUntested)
			continue;
		BMessage result;
		result.AddUInt32("codec", kCodecBits[i]);
		result.AddInt32("state", sTests[i].state);
		result.AddUInt32("variant", sTests[i].variant);
		result.AddString("reason", sTests[i].reason);
		archive.AddMessage("result", &result);
	}
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append(kTestCacheName) != B_OK)
		return;
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		archive.Flatten(&file);
}


static const nvenc_api*
nvenc_api_get()
{
	BAutolock _(sLock);
	if (sTried)
		return sApi;
	sTried = true;

	// TODO(linux backend): the nvenc add-on wraps air/OS's nvidia_rm
	// driver and is not built for Linux yet; there it would open NVIDIA's
	// libnvidia-encode.so.1 (NvEncodeAPICreateInstance) instead.
	const directory_which kDirectories[] = {
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY,
	};
	image_id image = -1;
	BPath path;
	for (size_t i = 0; i < sizeof(kDirectories) / sizeof(kDirectories[0]);
			i++) {
		if (find_directory(kDirectories[i], &path) != B_OK)
			continue;
		path.Append("media/encoders/nvenc");
		image = load_add_on(path.Path());
		if (image >= 0)
			break;
	}
	if (image < 0) {
		sReason = "The NVENC add-on is not installed.";
		return NULL;
	}

	nvenc_get_api_func getApi;
	if (get_image_symbol(image, NVENC_GET_API_SYMBOL, B_SYMBOL_TYPE_TEXT,
			(void**)&getApi) != B_OK) {
		unload_add_on(image);
		sReason = "The NVENC add-on is broken.";
		return NULL;
	}
	const nvenc_api* api = getApi();
	if (api == NULL || api->version != NVENC_API_VERSION) {
		unload_add_on(image);
		sReason = "The NVENC add-on is of another version.";
		return NULL;
	}

	char name[128] = "";
	char reason[256] = "";
	uint32_t codecs = 0;
	if (api->probe(&codecs, name, sizeof(name), reason, sizeof(reason)) != 0) {
		// keep the add-on loaded: it may say why
		sReason = reason[0] != '\0' ? reason : "NVENC did not start.";
		return NULL;
	}
	sCodecs = codecs;
	sName = name;
	sApi = api;

	struct stat st;
	if (stat(path.Path(), &st) != 0)
		st.st_mtime = 0;
	sCacheKey.SetToFormat("%s|%s|%lld|%d", sName.String(), path.Path(),
		(long long)st.st_mtime, NVENC_API_VERSION);
	load_test_results();
	return sApi;
}


// #pragma mark - self-test


static const int kTestWidth = 352;
static const int kTestHeight = 288;
static const int kTestPictures = 6;


/*!	A smooth ramp with a textured square moving across it: easy to encode
	well, so a working encoder comes back above 40 dB, and any mix-up of
	lines, planes or pictures shows at once. */
static void
make_test_picture(int index, bool tenBit, std::vector<uint8>& luma,
	std::vector<uint8>& chroma, std::vector<int>& reference)
{
	int bytes = tenBit ? 2 : 1;
	luma.assign((size_t)kTestWidth * kTestHeight * bytes, 0);
	chroma.assign((size_t)kTestWidth * kTestHeight / 2 * bytes, 0);
	reference.assign((size_t)kTestWidth * kTestHeight, 0);
	int squareX = 40 + 12 * index;
	int squareY = 60 + 6 * index;
	for (int y = 0; y < kTestHeight; y++) {
		for (int x = 0; x < kTestWidth; x++) {
			int value = 32 + x * 160 / kTestWidth + y * 40 / kTestHeight;
			if (x >= squareX && x < squareX + 64 && y >= squareY
				&& y < squareY + 64)
				value = 220 - ((x - squareX) + (y - squareY)) / 2;
			size_t at = (size_t)y * kTestWidth + x;
			reference[at] = tenBit ? value * 4 : value;
			if (tenBit) {
				uint16 sample = (uint16)(value * 4) << 6;
				memcpy(&luma[at * 2], &sample, 2);
			} else
				luma[at] = (uint8)value;
		}
	}
	for (int y = 0; y < kTestHeight / 2; y++) {
		for (int x = 0; x < kTestWidth / 2; x++) {
			int u = 128 + (x - kTestWidth / 4) / 4;
			int v = 128 + (y - kTestHeight / 4) / 2;
			if (2 * x >= squareX && 2 * x < squareX + 64 && 2 * y >= squareY
				&& 2 * y < squareY + 64) {
				u = 90;
				v = 170;
			}
			size_t at = ((size_t)y * (kTestWidth / 2) + x) * 2;
			if (tenBit) {
				uint16 cb = (uint16)(u * 4) << 6, cr = (uint16)(v * 4) << 6;
				memcpy(&chroma[at * 2], &cb, 2);
				memcpy(&chroma[at * 2 + 2], &cr, 2);
			} else {
				chroma[at] = (uint8)u;
				chroma[at + 1] = (uint8)v;
			}
		}
	}
}


/*!	Encodes the test pictures through the add-on with one of its ways of
	laying out the input, decodes them with libavcodec and compares. */
static bool
run_test(const nvenc_api* api, uint32 bit, uint32 variant, BString& why,
	bool& timedOut)
{
	timedOut = false;
	bool tenBit = bit == NVENC_CODEC_HEVC_10 || bit == NVENC_CODEC_AV1_10;
	bool av1 = bit == NVENC_CODEC_AV1 || bit == NVENC_CODEC_AV1_10;

	nvenc_config config;
	memset(&config, 0, sizeof(config));
	config.codec = bit;
	config.width = kTestWidth;
	config.height = kTestHeight;
	config.frame_rate_num = 30;
	config.frame_rate_den = 1;
	config.sar_num = 1;
	config.sar_den = 1;
	config.rate_control = NVENC_RC_CONSTQP;
	config.qp = av1 ? 80 : 22;
	config.key_interval = 30;
	config.preset = 4;
	config.colour_primaries = 1;
	config.transfer = 1;
	config.matrix = 1;
	config.variant = variant;
	config.timeout_ms = 1000;

	AVCodecID id = bit == NVENC_CODEC_H264 ? AV_CODEC_ID_H264
		: av1 ? AV_CODEC_ID_AV1 : AV_CODEC_ID_HEVC;
	const AVCodec* decoder = avcodec_find_decoder(id);
	AVCodecContext* context = decoder != NULL
		? avcodec_alloc_context3(decoder) : NULL;
	if (context == NULL || avcodec_open2(context, decoder, NULL) < 0) {
		avcodec_free_context(&context);
		why = "There is no decoder to check NVENC's output with.";
		return false;
	}

	char reason[256] = "";
	nvenc_session* session = api->open(&config, reason, sizeof(reason));
	if (session == NULL) {
		avcodec_free_context(&context);
		why = reason[0] != '\0' ? reason : "NVENC did not start.";
		return false;
	}

	std::vector<std::vector<int> > references(kTestPictures);
	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();
	double squared = 0;
	int64 samples = 0;
	int decoded = 0;
	bool failed = false;

	auto takeFrames = [&]() {
		while (avcodec_receive_frame(context, frame) == 0) {
			if (decoded < kTestPictures && frame->width >= kTestWidth
				&& frame->height >= kTestHeight) {
				const std::vector<int>& reference = references[decoded];
				bool wide = frame->format != AV_PIX_FMT_YUV420P
					&& frame->format != AV_PIX_FMT_YUVJ420P;
				for (int y = 0; y < kTestHeight; y++) {
					const uint8* line = frame->data[0] + (size_t)y * frame->linesize[0];
					for (int x = 0; x < kTestWidth; x++) {
						int value = wide ? ((const uint16*)line)[x] : line[x];
						double difference = value - reference[(size_t)y * kTestWidth + x];
						squared += difference * difference;
					}
				}
				samples += (int64)kTestWidth * kTestHeight;
			}
			decoded++;
			av_frame_unref(frame);
		}
	};
	auto takePackets = [&]() -> bool {
		nvenc_packet out;
		int status;
		while ((status = api->receive(session, &out)) == 0) {
			if (av_new_packet(packet, (int)out.size) == 0) {
				memcpy(packet->data, out.data, out.size);
				packet->pts = out.pts;
				packet->dts = out.dts;
				avcodec_send_packet(context, packet);
				av_packet_unref(packet);
			}
			api->release(session, &out);
			takeFrames();
		}
		return status != -1;
	};

	std::vector<uint8> luma, chroma;
	for (int i = 0; i < kTestPictures && !failed; i++) {
		make_test_picture(i, tenBit, luma, chroma, references[i]);
		nvenc_picture picture;
		memset(&picture, 0, sizeof(picture));
		picture.planes[0] = luma.data();
		picture.planes[1] = chroma.data();
		picture.strides[0] = kTestWidth * (tenBit ? 2 : 1);
		picture.strides[1] = picture.strides[0];
		picture.pts = i;
		int status = api->encode(session, &picture);
		if (status != 0) {
			why = api->describe(session);
			timedOut = status == NVENC_TIMED_OUT;
			failed = true;
		} else if (!takePackets()) {
			why = "NVENC lost a picture.";
			failed = true;
		}
	}
	if (!failed) {
		api->encode(session, NULL);
		takePackets();
		avcodec_send_packet(context, NULL);
		takeFrames();
	}
	api->close(session);
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&context);
	if (failed)
		return false;

	if (decoded < kTestPictures || samples == 0) {
		why.SetToFormat("Only %d of %d test pictures NVENC encoded could be "
			"decoded.", decoded, kTestPictures);
		return false;
	}
	double peak = tenBit ? 1023.0 : 255.0;
	double meanSquared = squared / samples;
	double psnr = meanSquared > 0
		? 10 * log10(peak * peak / meanSquared) : 99.0;
	if (psnr < 32) {
		why.SetToFormat("The test pictures NVENC encoded came back wrong "
			"(PSNR %.1f dB).", psnr);
		return false;
	}
	return true;
}


NvencEncoder::NvencEncoder(const char* deviceName)
	:
	fDeviceName(deviceName),
	fApi(NULL),
	fSession(NULL),
	fBitDepth(8),
	fDrained(false)
{
}


NvencEncoder::~NvencEncoder()
{
	if (fApi != NULL && fSession != NULL)
		fApi->close(fSession);
}


static uint32
nvenc_codec(video_codec codec)
{
	switch (codec) {
		case kVideoH264:
			return NVENC_CODEC_H264;
		case kVideoH265:
			return NVENC_CODEC_HEVC;
		case kVideoH265_10:
			return NVENC_CODEC_HEVC_10;
		case kVideoAV1:
			return NVENC_CODEC_AV1;
		case kVideoAV1_10:
			return NVENC_CODEC_AV1_10;
		default:
			return 0;
	}
}


bool
NvencEncoder::Implements(video_codec codec)
{
	uint32 bit = nvenc_codec(codec);
	if (bit == 0)
		return false;
	// The add-on decides for the card; with no add-on nothing is
	// implemented, and a format that failed the test is not either.
	if (nvenc_api_get() == NULL || (sCodecs & bit) == 0)
		return false;
	BAutolock _(sLock);
	return sTests[codec_slot(bit)].state != kFails;
}


bool
NvencEncoder::Tested(video_codec codec, bool* works, BString* why)
{
	uint32 bit = nvenc_codec(codec);
	if (bit == 0 || nvenc_api_get() == NULL)
		return false;
	BAutolock _(sLock);
	const TestResult& result = sTests[codec_slot(bit)];
	if (result.state == kUntested)
		return false;
	if (works != NULL)
		*works = result.state == kWorks;
	if (why != NULL)
		*why = result.reason;
	return true;
}


/*!	Tries the add-on's ways of laying out the input until one gives the
	test pictures back. */
static TestResult
test_in_this_process(const nvenc_api* api, uint32 bit)
{
	TestResult result;
	result.state = kFails;
	result.variant = 0;
	uint32 variants = api->variants != NULL ? api->variants(0, NULL) : 1;
	const char* forced = getenv("MEDIACONVERTER_NVENC_VARIANT");
	uint32 first = forced != NULL ? (uint32)atoi(forced) : 0;
	uint32 last = forced != NULL ? first + 1 : variants;
	BString firstReason;
	for (uint32 variant = first; variant < last && variant < variants;
			variant++) {
		BString reason;
		bool timedOut;
		if (run_test(api, bit, variant, reason, timedOut)) {
			result.state = kWorks;
			result.variant = variant;
			return result;
		}
		const char* name = NULL;
		if (api->variants != NULL)
			api->variants(variant, &name);
		fprintf(stderr, "NVENC self-test, %s: %s\n",
			name != NULL ? name : "default", reason.String());
		if (firstReason.IsEmpty())
			firstReason = reason;
		// an engine that does not answer at all will not for another layout
		if (timedOut)
			break;
	}
	result.reason = firstReason;
	return result;
}


static BString
own_executable()
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (info.type == B_APP_IMAGE)
			return BString(info.name);
	}
	return BString();
}


/*!	The test runs in a process of its own - this executable again, with
	--nvenc-self-test (see SelfTestMain()) - so a card or an add-on that
	crashes or hangs costs the format, not the application. */
static TestResult
test_in_child(uint32 bit)
{
	TestResult result;
	result.state = kFails;
	result.variant = 0;

	BString executable = own_executable();
	int fds[2];
	if (executable.IsEmpty() || pipe(fds) != 0) {
		result.reason = "The test could not be started.";
		return result;
	}
	char codec[16];
	snprintf(codec, sizeof(codec), "%" B_PRIu32, bit);
	pid_t child = fork();
	if (child == 0) {
		dup2(fds[1], STDOUT_FILENO);
		close(fds[0]);
		close(fds[1]);
		execl(executable.String(), executable.String(), "--nvenc-self-test",
			codec, (char*)NULL);
		_exit(127);
	}
	close(fds[1]);
	if (child < 0) {
		close(fds[0]);
		result.reason = "The test could not be started.";
		return result;
	}

	BString output;
	bool finished = false;
	bigtime_t deadline = system_time() + 90000000;
	while (true) {
		bigtime_t left = deadline - system_time();
		if (left <= 0)
			break;
		struct pollfd wait = { fds[0], POLLIN, 0 };
		int ready = poll(&wait, 1, (int)(left / 1000));
		if (ready < 0 && errno == EINTR)
			continue;
		if (ready <= 0)
			break;
		char buffer[512];
		ssize_t bytes = read(fds[0], buffer, sizeof(buffer));
		if (bytes <= 0) {
			finished = true;
			break;
		}
		output.Append(buffer, bytes);
	}
	close(fds[0]);
	if (!finished)
		kill(child, SIGKILL);
	int status = 0;
	waitpid(child, &status, 0);

	int32 at = output.FindFirst("works ");
	if (at >= 0) {
		result.state = kWorks;
		result.variant = (uint32)atoi(output.String() + at + 6);
		return result;
	}
	at = output.FindFirst("fails ");
	if (at >= 0) {
		output.Remove(0, at + 6);
		output.RemoveAll("\n");
		result.reason = output;
	} else if (!finished)
		result.reason = "The test did not finish in a minute and a half.";
	else if (WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0))
		result.reason = "The test crashed.";
	else
		result.reason = "The test said nothing.";
	return result;
}


int
NvencEncoder::SelfTestMain(const char* codec)
{
	// a crash ends this process quietly: the parent reads it as a failure
	// (Linux: no core dump, so systemd-coredump and debug_server's crash
	// dialog leave it alone; libroot2 has no disable_debugger())
	prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
	uint32 bit = (uint32)strtoul(codec, NULL, 10);
	const nvenc_api* api = nvenc_api_get();
	if (api == NULL) {
		printf("fails %s\n", sReason.String());
		return 0;
	}
	if (codec_slot(bit) < 0 || (sCodecs & bit) == 0) {
		printf("fails NVENC cannot encode this.\n");
		return 0;
	}
	TestResult result = test_in_this_process(api, bit);
	if (result.state == kWorks)
		printf("works %" B_PRIu32 "\n", result.variant);
	else
		printf("fails %s\n", result.reason.String());
	fflush(stdout);
	return 0;
}


bool
NvencEncoder::SelfTest(video_codec codec, BString* why)
{
	uint32 bit = nvenc_codec(codec);
	const nvenc_api* api = nvenc_api_get();
	if (bit == 0 || api == NULL || (sCodecs & bit) == 0) {
		if (why != NULL)
			*why = api == NULL ? sReason : BString("NVENC cannot encode this.");
		return false;
	}
	int slot = codec_slot(bit);

	// one test at a time; a second caller waits for the first one's result
	BAutolock testing(sTestLock);
	{
		BAutolock _(sLock);
		if (sTests[slot].state != kUntested) {
			if (why != NULL)
				*why = sTests[slot].reason;
			return sTests[slot].state == kWorks;
		}
	}

	TestResult result = getenv("MEDIACONVERTER_NVENC_IN_PROCESS") != NULL
		? test_in_this_process(api, bit) : test_in_child(bit);
	if (result.state != kWorks) {
		BString reason = result.reason;
		result.reason.SetToFormat("NVENC failed MediaConverter's test: %s",
			reason.String());
	}

	BAutolock _(sLock);
	sTests[slot] = result;
	save_test_results();
	if (why != NULL)
		*why = result.reason;
	return result.state == kWorks;
}


void
NvencEncoder::SelfTestAll()
{
	if (nvenc_api_get() == NULL)
		return;
	static const video_codec kCodecs[] = {
		kVideoH264, kVideoH265, kVideoH265_10, kVideoAV1, kVideoAV1_10
	};
	for (size_t i = 0; i < sizeof(kCodecs) / sizeof(kCodecs[0]); i++) {
		if ((sCodecs & nvenc_codec(kCodecs[i])) != 0)
			SelfTest(kCodecs[i]);
	}
}


bool
NvencEncoder::IsAvailable(BString* why)
{
	bool available = nvenc_api_get() != NULL;
	if (!available && why != NULL)
		*why = sReason;
	return available;
}


uint32
NvencEncoder::Codecs()
{
	return nvenc_api_get() != NULL ? sCodecs : 0;
}


status_t
NvencEncoder::Open(const VideoEncoderParams& params, BString& error)
{
	fApi = nvenc_api_get();
	if (fApi == NULL) {
		error = sReason;
		return B_NOT_SUPPORTED;
	}
	// nothing is encoded on a card before it has shown it can
	if (!SelfTest(params.codec, &error))
		return B_NOT_SUPPORTED;
	if (!sName.IsEmpty())
		fDeviceName = sName;
	fBitDepth = video_codec_info(params.codec).bitDepth;

	nvenc_config config;
	memset(&config, 0, sizeof(config));
	config.codec = nvenc_codec(params.codec);
	config.width = params.width;
	config.height = params.height;
	config.frame_rate_num = params.frameRate.num;
	config.frame_rate_den = params.frameRate.den;
	if (params.sampleAspect.num > 0) {
		config.sar_num = params.sampleAspect.num;
		config.sar_den = params.sampleAspect.den;
	}
	bool av1 = params.codec == kVideoAV1 || params.codec == kVideoAV1_10;
	if (params.rateMode == kRateConstantQuality) {
		config.rate_control = NVENC_RC_CONSTQP;
		config.qp = av1 ? quality_to_crf(params.quality, 60, 180)
			: quality_to_crf(params.quality, 16, 36);
	} else {
		config.rate_control = NVENC_RC_VBR;
		config.bitrate = (uint32)params.bitrate * 1000;
		config.max_bitrate = config.bitrate / 2 * 3;
	}
	config.key_interval = params.keyInterval;
	static const uint32 kPresets[kSpeedCount] = { 1, 2, 4, 6, 7 };
	config.preset = kPresets[params.speed];
	config.colour_primaries = params.primaries;
	config.transfer = params.transfer;
	config.matrix = params.matrix;
	config.full_range = params.range == AVCOL_RANGE_JPEG;
	config.chroma_location = params.chromaLocation > 0
		? params.chromaLocation - 1 : 0;
	{
		BAutolock _(sLock);
		config.variant = sTests[codec_slot(config.codec)].variant;
	}

	char reason[256] = "";
	fSession = fApi->open(&config, reason, sizeof(reason));
	if (fSession == NULL) {
		error = reason[0] != '\0' ? reason : "NVENC did not start.";
		return B_ERROR;
	}

	uint8 headers[1024];
	size_t size = sizeof(headers);
	if (fApi->headers(fSession, headers, &size) == 0 && size > 0)
		fExtradata.assign(headers, headers + size);
	return B_OK;
}


AVPixelFormat
NvencEncoder::InputFormat() const
{
	return fBitDepth > 8 ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
}


status_t
NvencEncoder::SendFrame(const AVFrame* frame)
{
	if (frame == NULL)
		return fApi->encode(fSession, NULL) == 0 ? B_OK : B_ERROR;

	nvenc_picture picture;
	memset(&picture, 0, sizeof(picture));
	picture.planes[0] = frame->data[0];
	picture.planes[1] = frame->data[1];
	picture.strides[0] = frame->linesize[0];
	picture.strides[1] = frame->linesize[1];
	picture.pts = frame->pts;
	picture.force_key = frame->pict_type == AV_PICTURE_TYPE_I;
	return fApi->encode(fSession, &picture) == 0 ? B_OK : B_ERROR;
}


status_t
NvencEncoder::ReceivePacket(AVPacket* packet)
{
	if (fDrained)
		return B_LAST_BUFFER_ERROR;
	nvenc_packet result;
	memset(&result, 0, sizeof(result));
	int status = fApi->receive(fSession, &result);
	if (status == 1)
		return B_WOULD_BLOCK;
	if (status == 2) {
		fDrained = true;
		return B_LAST_BUFFER_ERROR;
	}
	if (status != 0)
		return B_ERROR;

	status_t error = B_OK;
	if (av_new_packet(packet, (int)result.size) == 0) {
		memcpy(packet->data, result.data, result.size);
		packet->pts = result.pts;
		packet->dts = result.dts;
		if (result.key)
			packet->flags |= AV_PKT_FLAG_KEY;
	} else
		error = B_NO_MEMORY;
	fApi->release(fSession, &result);
	return error;
}


encoder_backend
NvencEncoder::Backend() const
{
	return kBackendNVENC;
}


BString
NvencEncoder::Name() const
{
	BString name;
	name.SetToFormat("%s (NVENC)", fDeviceName.String());
	return name;
}


BString
NvencEncoder::Description() const
{
	if (fApi == NULL || fSession == NULL)
		return BString();
	return BString(fApi->describe(fSession));
}
