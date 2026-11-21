/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "MppVideoEncoder.h"

#include <dlfcn.h>
#include <new>
#include <string.h>
#include <unistd.h>

#include <Locker.h>
#include <Autolock.h>


// Linux: librockchip_mpp drives the encoder through the kernel's MPP
// service node (air/OS: its rk3588_vpu driver)
static const char* kDevice = "/dev/mpp_service";
static const char* kTag = "mediaconverter";


template<typename T>
static bool
find_function(void* image, const char* name, T& function)
{
	function = (T)dlsym(image, name);
	return function != NULL;
}


const MppLibrary*
MppLibrary::Get(const char** error)
{
	static BLocker sLock("mpp library");
	static bool sTried = false;
	static const char* sError = NULL;
	static MppLibrary sLibrary;

	BAutolock _(sLock);
	if (!sTried) {
		sTried = true;
		void* image = dlopen("librockchip_mpp.so.1", RTLD_NOW);
		MppLibrary& l = sLibrary;
		if (image == NULL)
			sError = "Rockchip MPP (librockchip_mpp.so.1) is not installed.";
		else if (!find_function(image, "mpp_create", l.create)
			|| !find_function(image, "mpp_init", l.init)
			|| !find_function(image, "mpp_destroy", l.destroy)
			|| !find_function(image, "mpp_enc_cfg_init", l.cfg_init)
			|| !find_function(image, "mpp_enc_cfg_deinit", l.cfg_deinit)
			|| !find_function(image, "mpp_enc_cfg_set_s32", l.cfg_set_s32)
			|| !find_function(image, "mpp_frame_init", l.frame_init)
			|| !find_function(image, "mpp_frame_deinit", l.frame_deinit)
			|| !find_function(image, "mpp_frame_set_width", l.frame_set_width)
			|| !find_function(image, "mpp_frame_set_height",
				l.frame_set_height)
			|| !find_function(image, "mpp_frame_set_hor_stride",
				l.frame_set_hor_stride)
			|| !find_function(image, "mpp_frame_set_ver_stride",
				l.frame_set_ver_stride)
			|| !find_function(image, "mpp_frame_set_fmt", l.frame_set_fmt)
			|| !find_function(image, "mpp_frame_set_pts", l.frame_set_pts)
			|| !find_function(image, "mpp_frame_set_eos", l.frame_set_eos)
			|| !find_function(image, "mpp_frame_set_buffer",
				l.frame_set_buffer)
			|| !find_function(image, "mpp_packet_init_with_buffer",
				l.packet_init_with_buffer)
			|| !find_function(image, "mpp_packet_deinit", l.packet_deinit)
			|| !find_function(image, "mpp_packet_set_length",
				l.packet_set_length)
			|| !find_function(image, "mpp_packet_get_pos", l.packet_get_pos)
			|| !find_function(image, "mpp_packet_get_length",
				l.packet_get_length)
			|| !find_function(image, "mpp_packet_get_pts", l.packet_get_pts)
			|| !find_function(image, "mpp_packet_has_meta",
				l.packet_has_meta)
			|| !find_function(image, "mpp_packet_get_meta",
				l.packet_get_meta)
			|| !find_function(image, "mpp_meta_get_s32", l.meta_get_s32)
			|| !find_function(image, "mpp_buffer_group_get",
				l.buffer_group_get)
			|| !find_function(image, "mpp_buffer_group_put",
				l.buffer_group_put)
			|| !find_function(image, "mpp_buffer_get_with_tag",
				l.buffer_get_with_tag)
			|| !find_function(image, "mpp_buffer_put_with_caller",
				l.buffer_put_with_caller)
			|| !find_function(image, "mpp_buffer_get_ptr_with_caller",
				l.buffer_get_ptr_with_caller)) {
			sError = "Rockchip MPP lacks a function the encoder needs.";
			dlclose(image);
		}
	}
	if (error != NULL)
		*error = sError;
	return sError == NULL ? &sLibrary : NULL;
}


// #pragma mark -


MppVideoEncoder::MppVideoEncoder(const char* deviceName)
	:
	fLibrary(NULL),
	fDeviceName(deviceName),
	fContext(NULL),
	fApi(NULL),
	fGroup(NULL),
	fPicture(NULL),
	fWidth(0),
	fHeight(0),
	fStride(0),
	fRows(0),
	fPictures(0),
	fDrained(false)
{
}


MppVideoEncoder::~MppVideoEncoder()
{
	if (fLibrary != NULL) {
		if (fContext != NULL)
			fLibrary->destroy(fContext);
		if (fPicture != NULL)
			fLibrary->buffer_put_with_caller(fPicture, kTag);
		if (fGroup != NULL)
			fLibrary->buffer_group_put(fGroup);
	}
	for (AVPacket* packet : fPackets)
		av_packet_free(&packet);
}


bool
MppVideoEncoder::Implements(video_codec codec)
{
	return codec == kVideoH264 || codec == kVideoH265;
}


bool
MppVideoEncoder::IsAvailable(BString* why)
{
	static BLocker sLock("mpp probe");
	static int sResult = -1;
	static BString sWhy;

	BAutolock _(sLock);
	if (sResult < 0) {
		sResult = 0;
		const char* error = NULL;
		const MppLibrary* library = NULL;
		if (access(kDevice, F_OK) != 0)
			sWhy = "No RK3588 video codec driver (rk3588_vpu).";
		else if ((library = MppLibrary::Get(&error)) == NULL)
			sWhy = error;
		else {
			// A driver from before the encoder refuses MPP's encoder
			// client, and mpp_init() says so.
			MppCtx context = NULL;
			MppApi* api = NULL;
			if (library->create(&context, &api) != kMppOK)
				sWhy = "Rockchip MPP made no context.";
			else if (library->init(context, kMppContextEncoder, kMppCodingAVC)
					!= kMppOK) {
				sWhy = "The rk3588_vpu driver does not encode (an older "
					"driver, or encode_profile is off).";
			} else
				sResult = 1;
			if (context != NULL)
				library->destroy(context);
		}
	}
	if (why != NULL)
		*why = sWhy;
	return sResult == 1;
}


status_t
MppVideoEncoder::_Configure(const VideoEncoderParams& params, BString& error)
{
	MppEncCfg config = NULL;
	if (fLibrary->cfg_init(&config) != kMppOK) {
		error = "Rockchip MPP made no encoder configuration.";
		return B_NO_MEMORY;
	}
	auto set = [&](const char* name, int32 value) {
		fLibrary->cfg_set_s32(config, name, value);
	};

	bool hevc = params.codec == kVideoH265;
	set("codec:type", hevc ? kMppCodingHEVC : kMppCodingAVC);
	set("prep:width", fWidth);
	set("prep:height", fHeight);
	set("prep:hor_stride", fStride);
	set("prep:ver_stride", fRows);
	set("prep:format", kMppFormatNV12);
	// MPP's colour descriptions are the H.273 code points FFmpeg uses.
	if (params.matrix != AVCOL_SPC_UNSPECIFIED)
		set("prep:colorspace", params.matrix);
	if (params.primaries != AVCOL_PRI_UNSPECIFIED)
		set("prep:colorprim", params.primaries);
	if (params.transfer != AVCOL_TRC_UNSPECIFIED)
		set("prep:colortrc", params.transfer);
	set("prep:colorrange", params.range == AVCOL_RANGE_JPEG ? 2 : 1);

	int32 fpsNum = 30;
	int32 fpsDen = 1;
	if (params.frameRate.num > 0 && params.frameRate.den > 0) {
		fpsNum = params.frameRate.num;
		fpsDen = params.frameRate.den;
	}
	set("rc:fps_in_flex", 0);
	set("rc:fps_in_num", fpsNum);
	set("rc:fps_in_denom", fpsDen);
	set("rc:fps_out_flex", 0);
	set("rc:fps_out_num", fpsNum);
	set("rc:fps_out_denom", fpsDen);
	int32 gop = params.keyInterval > 0 ? params.keyInterval
		: (int32)((2 * fpsNum + fpsDen - 1) / fpsDen);
	set("rc:gop", gop);

	if (params.rateMode == kRateAverageBitrate) {
		int32 bps = params.bitrate * 1000;
		set("rc:mode", kMppRateVBR);
		set("rc:bps_target", bps);
		set("rc:bps_max", bps / 16 * 17);
		set("rc:bps_min", bps / 16);
		set("rc:qp_init", -1);
		set("rc:qp_max", 51);
		set("rc:qp_min", 10);
		set("rc:qp_max_i", 51);
		set("rc:qp_min_i", 10);
		set("rc:qp_ip", 2);
		fDescription.SetToFormat("%s, VBR %" B_PRId32 " kbit/s, IDR every %"
			B_PRId32 ", IPPP", hevc ? "HEVC Main" : "H.264 High",
			params.bitrate, gop);
	} else {
		// what NVENC and the Cedar encoder map quality to
		int32 qp = quality_to_crf(params.quality, 16, 40);
		set("rc:mode", kMppRateFixedQP);
		set("rc:qp_init", qp);
		set("rc:qp_max", qp);
		set("rc:qp_min", qp);
		set("rc:qp_max_i", qp);
		set("rc:qp_min_i", qp);
		set("rc:qp_ip", 0);
		set("rc:fqp_min_i", qp);
		set("rc:fqp_max_i", qp);
		set("rc:fqp_min_p", qp);
		set("rc:fqp_max_p", qp);
		fDescription.SetToFormat("%s, CQP %" B_PRId32 ", IDR every %"
			B_PRId32 ", IPPP", hevc ? "HEVC Main" : "H.264 High", qp, gop);
	}
	if (!hevc) {
		set("h264:profile", 100);
		set("h264:level", fWidth * fHeight > 2048 * 1088 ? 51 : 41);
		set("h264:cabac_en", 1);
		set("h264:cabac_idc", 0);
		set("h264:trans8x8", 1);
	}

	MPP_RET result = fApi->control(fContext, kMppEncoderSetConfig, config);
	fLibrary->cfg_deinit(config);
	if (result != kMppOK) {
		error.SetToFormat("Rockchip MPP refused the encoder settings (%d).",
			(int)result);
		return B_BAD_VALUE;
	}

	// Parameter sets before every IDR picture as well, for streams that are
	// cut or joined; the container still gets them as extradata.
	int32 headerMode = kMppHeaderEachIDR;
	fApi->control(fContext, kMppEncoderHeaderMode, &headerMode);
	return B_OK;
}


status_t
MppVideoEncoder::Open(const VideoEncoderParams& params, BString& error)
{
	if (!Implements(params.codec)) {
		error = "The RK3588 encodes H.264 and H.265 (eight bits) only.";
		return B_NOT_SUPPORTED;
	}
	if (params.width < 16 || params.height < 16 || params.width > 8192
		|| params.height > 8192) {
		error = "The RK3588 encodes pictures of 16x16 to 8192x8192.";
		return B_NOT_SUPPORTED;
	}
	const char* why = NULL;
	fLibrary = MppLibrary::Get(&why);
	if (fLibrary == NULL) {
		error = why;
		return B_NOT_SUPPORTED;
	}
	fWidth = params.width;
	fHeight = params.height;
	fStride = (fWidth + 15) & ~15;
	fRows = (fHeight + 15) & ~15;

	if (fLibrary->create(&fContext, &fApi) != kMppOK || fApi == NULL) {
		fContext = NULL;
		error = "Rockchip MPP made no context.";
		return B_ERROR;
	}
	// Wait for each packet, but not for ever.
	int32 timeout = 2000;
	fApi->control(fContext, kMppSetOutputTimeout, &timeout);
	if (fLibrary->init(fContext, kMppContextEncoder,
			params.codec == kVideoH265 ? kMppCodingHEVC : kMppCodingAVC)
			!= kMppOK) {
		error = "Rockchip MPP has no encoder for this (is the rk3588_vpu "
			"driver's encoder enabled?).";
		return B_NOT_SUPPORTED;
	}
	status_t status = _Configure(params, error);
	if (status != B_OK)
		return status;

	if (fLibrary->buffer_group_get(&fGroup, kMppBufferTypeDRM,
			kMppBufferInternal, kTag, kTag) != kMppOK
		|| fLibrary->buffer_get_with_tag(fGroup, &fPicture,
			(size_t)fStride * fRows * 3 / 2, kTag, kTag) != kMppOK) {
		error = "The rk3588_vpu driver gave no picture buffer.";
		return B_NO_MEMORY;
	}

	// The parameter sets, for the container.
	MppBuffer headerBuffer = NULL;
	if (fLibrary->buffer_get_with_tag(fGroup, &headerBuffer, 64 * 1024, kTag,
			kTag) == kMppOK) {
		MppPacket packet = NULL;
		if (fLibrary->packet_init_with_buffer(&packet, headerBuffer)
				== kMppOK) {
			fLibrary->packet_set_length(packet, 0);
			if (fApi->control(fContext, kMppEncoderGetHeaders, packet)
					== kMppOK) {
				const uint8* data
					= (const uint8*)fLibrary->packet_get_pos(packet);
				size_t length = fLibrary->packet_get_length(packet);
				if (data != NULL && length > 0)
					fExtradata.assign(data, data + length);
			}
			fLibrary->packet_deinit(&packet);
		}
		fLibrary->buffer_put_with_caller(headerBuffer, kTag);
	}
	if (fExtradata.empty()) {
		error = "Rockchip MPP gave no parameter sets.";
		return B_ERROR;
	}
	return B_OK;
}


AVPixelFormat
MppVideoEncoder::InputFormat() const
{
	return AV_PIX_FMT_NV12;
}


status_t
MppVideoEncoder::_TakePacket()
{
	MppPacket result = NULL;
	MPP_RET status = fApi->encode_get_packet(fContext, &result);
	if (status != kMppOK || result == NULL)
		return status == kMppErrorTimeout || result == NULL ? B_TIMED_OUT
			: B_ERROR;

	const uint8* data = (const uint8*)fLibrary->packet_get_pos(result);
	size_t length = fLibrary->packet_get_length(result);
	AVPacket* packet = av_packet_alloc();
	if (packet == NULL || data == NULL || av_new_packet(packet, (int)length) != 0) {
		av_packet_free(&packet);
		fLibrary->packet_deinit(&result);
		return B_NO_MEMORY;
	}
	memcpy(packet->data, data, length);
	packet->pts = fLibrary->packet_get_pts(result);
	packet->dts = packet->pts;	// no B pictures
	int32 intra = 0;
	if (fLibrary->packet_has_meta(result)) {
		MppMeta meta = fLibrary->packet_get_meta(result);
		if (meta != NULL)
			fLibrary->meta_get_s32(meta, kMppMetaOutputIntra, &intra);
	}
	if (intra != 0)
		packet->flags |= AV_PKT_FLAG_KEY;
	fLibrary->packet_deinit(&result);
	fPackets.push_back(packet);
	return B_OK;
}


status_t
MppVideoEncoder::SendFrame(const AVFrame* frame)
{
	if (fContext == NULL || fPicture == NULL)
		return B_NO_INIT;
	if (frame == NULL) {
		// Every picture's packet came back as it was sent.
		fDrained = true;
		return B_OK;
	}
	if (frame->format != AV_PIX_FMT_NV12 || frame->width != fWidth
		|| frame->height != fHeight)
		return B_BAD_VALUE;

	uint8* picture = (uint8*)fLibrary->buffer_get_ptr_with_caller(fPicture,
		kTag);
	if (picture == NULL)
		return B_NO_MEMORY;
	for (int row = 0; row < fHeight; row++) {
		memcpy(picture + (size_t)row * fStride,
			frame->data[0] + (size_t)row * frame->linesize[0], fWidth);
	}
	uint8* chroma = picture + (size_t)fStride * fRows;
	for (int row = 0; row < (fHeight + 1) / 2; row++) {
		memcpy(chroma + (size_t)row * fStride,
			frame->data[1] + (size_t)row * frame->linesize[1], fWidth);
	}

	if (frame->pict_type == AV_PICTURE_TYPE_I && fPictures > 0)
		fApi->control(fContext, kMppEncoderSetIDR, NULL);

	MppFrame input = NULL;
	if (fLibrary->frame_init(&input) != kMppOK)
		return B_NO_MEMORY;
	fLibrary->frame_set_width(input, fWidth);
	fLibrary->frame_set_height(input, fHeight);
	fLibrary->frame_set_hor_stride(input, fStride);
	fLibrary->frame_set_ver_stride(input, fRows);
	fLibrary->frame_set_fmt(input, kMppFormatNV12);
	fLibrary->frame_set_pts(input, frame->pts);
	fLibrary->frame_set_eos(input, 0);
	fLibrary->frame_set_buffer(input, fPicture);
	MPP_RET result = fApi->encode_put_frame(fContext, input);
	fLibrary->frame_deinit(&input);
	if (result != kMppOK)
		return B_ERROR;
	fPictures++;
	// One packet a picture, before the buffer is filled again.
	return _TakePacket();
}


status_t
MppVideoEncoder::ReceivePacket(AVPacket* packet)
{
	if (fContext == NULL)
		return B_NO_INIT;
	if (fPackets.empty())
		return fDrained ? B_LAST_BUFFER_ERROR : B_WOULD_BLOCK;
	AVPacket* next = fPackets.front();
	fPackets.pop_front();
	av_packet_move_ref(packet, next);
	av_packet_free(&next);
	return B_OK;
}


encoder_backend
MppVideoEncoder::Backend() const
{
	return kBackendRockchip;
}


BString
MppVideoEncoder::Name() const
{
	BString name;
	name.SetToFormat("%s (RKVENC)", fDeviceName.String());
	return name;
}


BString
MppVideoEncoder::Description() const
{
	return fDescription;
}
