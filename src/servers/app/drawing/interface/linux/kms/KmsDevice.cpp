/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * The outputs, layouts and CRTC assignment follow air/OS's amdgpu_kms
 * accelerant (Copyright 2026, air/OS, MIT License); the flip failure
 * policy is VitruvianOS's (DrmFlipPolicy.h).
 */


#include "KmsDevice.h"

#include "DrmFlipPolicy.h"
#include "KmsBuffer.h"
#include "KmsEdid.h"
#include "SplashHandoff.h"
#ifdef VOS_HAVE_KMS_EGL
#	include "KmsGles.h"
#endif

#include <new>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#ifdef VOS_HAVE_KMS_GBM
#	include <gbm.h>
#endif
#ifdef VOS_HAVE_KMS_UDEV
#	include <libudev.h>
#endif


#define TRACE(x...) fprintf(stderr, "kms: " x)

#ifndef min_c
#	define min_c(a, b) ((a) > (b) ? (b) : (a))
#	define max_c(a, b) ((a) > (b) ? (a) : (b))
#endif


static const int32 kMaxSlots = 3;

// the device whose events the event thread takes (drmHandleEvent()'s
// handlers get no device of their own)
static __thread KmsDevice* sEventDevice = NULL;
static const uint32 kMaxDamageRects = 16;

enum {
	kEventFlip = 1,
	kEventBlank = 2
};

enum {
	kPlaneOverlay = 0,
	kPlanePrimary = 1,
	kPlaneCursor = 2
};


static status_t
errno_status()
{
	return errno > 0 ? -errno : B_ERROR;
}


bigtime_t
kms_monotonic_time()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (bigtime_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}


static uint64
event_data(uint32 kind, uint32 index, uint32 generation)
{
	return ((uint64)generation << 16) | ((index & 0xff) << 8) | kind;
}


static uint32
vblank_crtc_bits(uint32 index)
{
	if (index == 0)
		return 0;
	if (index == 1)
		return DRM_VBLANK_SECONDARY;
	return (index << DRM_VBLANK_HIGH_CRTC_SHIFT) & DRM_VBLANK_HIGH_CRTC_MASK;
}


// #pragma mark - structures


struct KmsDevice::Crtc {
	uint32				id;
	uint32				index;
	uint32				primaryPlane;
	uint32				cursorPlane;
	uint32				modeId;			// properties
	uint32				active;
	uint32				fbId;			// of the primary plane
	uint32				crtcId;
	uint32				srcX, srcY, srcW, srcH;
	uint32				crtcX, crtcY, crtcW, crtcH;
	uint32				cursorFbId;		// of the cursor plane
	uint32				cursorCrtcId;
	uint32				modeBlob;
};


struct KmsDevice::Output {
	uint32				connector;
	uint32				type;
	char				name[B_DISPLAY_OUTPUT_NAME_LENGTH];
	bool				connected;
	bool				internal;
	uint32				possibleCrtcs;
	uint32				widthMM;
	uint32				heightMM;
	std::vector<drmModeModeInfo> modes;
	int32				preferred;
	uint8				edid[B_DISPLAY_OUTPUT_EDID_LENGTH];
	uint32				edidLength;
	uint32				crtcIdProperty;
	uint32				dpmsProperty;

	// driven
	int32				crtcIndex;
	bool				blankArmed;
	bigtime_t			lastBlank;
	KmsBuffer*			cursor;
	bool				cursorShown;
	bool				gammaSet;

	Output()
		:
		connector(0), type(0), connected(false), internal(false),
		possibleCrtcs(0), widthMM(0), heightMM(0), preferred(-1),
		edidLength(0), crtcIdProperty(0), dpmsProperty(0), crtcIndex(-1),
		blankArmed(false), lastBlank(0), cursor(NULL), cursorShown(false),
		gammaSet(false)
	{
		name[0] = '\0';
	}
};


/*!	One ring of scan-out buffers, shown by one CRTC or (mirrors) several. */
struct KmsDevice::Scanout {
	uint32				index;
	KmsRect				region;			// of the frame buffer
	uint32				width;			// the buffers
	uint32				height;
	KmsRect				destination;	// region's place in the buffer
	bool				resample;		// filtered to the mode in the buffer
	bool				planeScaled;	// the display engine scales it
	std::vector<uint32>	outputs;		// connectors
	KmsScaler			scaler;

	std::vector<KmsBuffer*> buffers;	// the CPU's ring
	std::vector<KmsRect> stale[kMaxSlots];	// per buffer, buffer pixels
	KmsGlTarget*		gl;
	std::vector<KmsBuffer*> glBuffers;	// the surface's, wrapped
	std::vector<gbm_bo*> release;		// for the present thread

	KmsBuffer*			shown;
	KmsBuffer*			pending;		// flip under way
	KmsBuffer*			queued;			// drawn, not flipped yet
	uint32				pendingEvents;

	std::vector<KmsRect> damage;		// frame buffer pixels
	bool				full;
	bool				rendering;

	Scanout()
		:
		index(0), width(0), height(0), resample(false), planeScaled(false),
		gl(NULL), shown(NULL), pending(NULL), queued(NULL), pendingEvents(0),
		full(true), rendering(false)
	{
	}
};


// #pragma mark - KmsDevice


KmsDevice::KmsDevice(uint32 flags)
	:
	fFlags(flags),
	fAtomic(false),
	fDumb(false),
	fMaxWidth(0),
	fMaxHeight(0),
	fCursorWidth(64),
	fCursorHeight(64),
	fOutputsChanged(false),
	fModeSet(false),
	fHasPending(false),
	fGeneration(1),
	fBlankGeneration(1),
	fSplashQuit(false),
	fDPMSMode(B_DPMS_ON),
	fTemperature(6500.0f),
	fFirstModeset(true),
	fGbm(NULL),
	fGles(NULL),
	fGlesFailed(false),
	fForceBuffers(false),
	fUseGbmBuffers(false),
	fSlots(kMaxSlots),
	fCursorImageWidth(0),
	fCursorImageHeight(0),
	fCursorHotX(0),
	fCursorHotY(0),
	fCursorX(0),
	fCursorY(0),
	fCursorVisible(false),
	fCursorOk(false),
	fEventThreadRunning(false),
	fPresentThreadRunning(false),
	fQuit(false),
	fWakeFd(-1),
	fRendering(false),
	fRetraceWantedUntil(0),
	fFlips(0),
	fFlipFailures(0),
	fFlipBroken(false),
	fFrontSource(NULL),
	fListener(NULL),
	fUdev(NULL),
	fUdevMonitor(NULL),
	fDevice(0)
{
	fDriverName[0] = '\0';
	memset(&fCurrent, 0, sizeof(fCurrent));
	memset(&fPending, 0, sizeof(fPending));
	memset(&fMode, 0, sizeof(fMode));
	pthread_mutex_init(&fLock, NULL);
	pthread_cond_init(&fCondition, NULL);
}


KmsDevice::~KmsDevice()
{
	pthread_mutex_lock(&fLock);
	fQuit = true;
	pthread_cond_broadcast(&fCondition);
	pthread_mutex_unlock(&fLock);
	_Wake();
	if (fPresentThreadRunning)
		pthread_join(fPresentThread, NULL);
	if (fEventThreadRunning)
		pthread_join(fEventThread, NULL);

	pthread_mutex_lock(&fLock);
	if (fSession.IsActive())
		_Drain(200000);
	_HideCursors();
	_DestroyScanouts(fScanouts);
	for (size_t i = 0; i < fCrtcs.size(); i++) {
		if (fCrtcs[i].modeBlob != 0)
			drmModeDestroyPropertyBlob(Fd(), fCrtcs[i].modeBlob);
	}
	for (size_t i = 0; i < fOutputs.size(); i++)
		delete fOutputs[i].cursor;
	pthread_mutex_unlock(&fLock);

#ifdef VOS_HAVE_KMS_EGL
	delete fGles;
#endif
#ifdef VOS_HAVE_KMS_GBM
	if (fGbm != NULL)
		gbm_device_destroy(fGbm);
#endif
#ifdef VOS_HAVE_KMS_UDEV
	if (fUdevMonitor != NULL)
		udev_monitor_unref(fUdevMonitor);
	if (fUdev != NULL)
		udev_unref(fUdev);
#endif
	if (fWakeFd >= 0)
		close(fWakeFd);
	fSession.Close();
	pthread_cond_destroy(&fCondition);
	pthread_mutex_destroy(&fLock);
}


/*static*/ KmsDevice*
KmsDevice::Open(const char* path, uint32 flags, status_t* _error)
{
	KmsDevice* device = new(std::nothrow) KmsDevice(flags);
	status_t error = device != NULL ? device->_Init(path) : B_NO_MEMORY;
	if (error != B_OK) {
		delete device;
		device = NULL;
	}
	if (_error != NULL)
		*_error = error;
	return device;
}


status_t
KmsDevice::_Init(const char* path)
{
	status_t status = fSession.Open(path, this);
	if (status != B_OK)
		return status;
	int fd = Fd();

	drmVersion* version = drmGetVersion(fd);
	if (version != NULL) {
		strlcpy(fDriverName, version->name, sizeof(fDriverName));
		drmFreeVersion(version);
	}
	struct stat st;
	if (fstat(fd, &st) == 0)
		fDevice = st.st_rdev;

	uint64_t value = 0;
	fDumb = drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &value) == 0 && value != 0;
	if (drmGetCap(fd, DRM_CAP_CURSOR_WIDTH, &value) == 0 && value != 0)
		fCursorWidth = value;
	if (drmGetCap(fd, DRM_CAP_CURSOR_HEIGHT, &value) == 0 && value != 0)
		fCursorHeight = value;
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	const char* atomic = getenv("VOS_KMS_ATOMIC");
	if (atomic == NULL || strcmp(atomic, "0") != 0)
		fAtomic = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0;

	const char* slots = getenv("VOS_KMS_SLOTS");
	if (slots != NULL && atoi(slots) >= 2 && atoi(slots) <= kMaxSlots)
		fSlots = atoi(slots);

	status = _Probe();
	if (status != B_OK)
		return status;

#ifdef VOS_HAVE_KMS_GBM
	const char* buffers = getenv("VOS_KMS_BUFFERS");
	fForceBuffers = buffers != NULL;
	bool wantGbm = buffers == NULL || strcmp(buffers, "dumb") != 0;
	fGbm = gbm_create_device(fd);
	fUseGbmBuffers = fGbm != NULL && wantGbm
		&& (buffers != NULL || !fDumb);
		// GBM buffer objects mapped by the CPU are the fallback for drivers
		// without dumb buffers; VOS_KMS_BUFFERS=gbm asks for them anyway
#	ifdef VOS_HAVE_KMS_EGL
	const char* gpu = getenv("VOS_KMS_GPU");
	if (fGbm != NULL && (fFlags & OPEN_NO_GPU) == 0
		&& (gpu == NULL || strcmp(gpu, "0") != 0)) {
		fGles = new(std::nothrow) KmsGles;
		if (fGles != NULL && fGles->Init(fGbm) != B_OK) {
			delete fGles;
			fGles = NULL;
		}
		if (fGles != NULL && fGles->Renderer()[0] != '\0'
			&& fGles->IsSoftware() && (gpu == NULL || strcmp(gpu, "1") != 0)) {
			// a GPU pass on the CPU is slower than the CPU path
			TRACE("GPU pass on %s: the CPU path is used instead"
				" (VOS_KMS_GPU=1 uses it)\n", fGles->Renderer());
			delete fGles;
			fGles = NULL;
		}
	}
#	endif
#endif

	fWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

#ifdef VOS_HAVE_KMS_UDEV
	fUdev = udev_new();
	if (fUdev != NULL) {
		fUdevMonitor = udev_monitor_new_from_netlink(fUdev, "udev");
		if (fUdevMonitor != NULL) {
			udev_monitor_filter_add_match_subsystem_devtype(fUdevMonitor,
				"drm", NULL);
			udev_monitor_enable_receiving(fUdevMonitor);
		}
	}
#endif

	TRACE("%s (%s, %s): %s modesetting, %zu CRTCs, %zu connectors, "
		"cursor %ux%u, scan-out on %s%s%s\n", Path(), fDriverName,
		fSession.How(), fAtomic ? "atomic" : "legacy", fCrtcs.size(),
		fOutputs.size(), fCursorWidth, fCursorHeight,
		UsesGpu() ? "the GPU (" : fUseGbmBuffers ? "GBM buffers"
			: "dumb buffers", UsesGpu() ? GpuRenderer() : "",
		UsesGpu() ? ")" : "");

	if (pthread_create(&fEventThread, NULL, _EventThreadEntry, this) != 0)
		return B_NO_MORE_THREADS;
	fEventThreadRunning = true;
	if (pthread_create(&fPresentThread, NULL, _PresentThreadEntry, this) != 0)
		return B_NO_MORE_THREADS;
	fPresentThreadRunning = true;
	return B_OK;
}


bool
KmsDevice::UsesGpu() const
{
#ifdef VOS_HAVE_KMS_EGL
	return fGles != NULL && !fGlesFailed;
#else
	return false;
#endif
}


const char*
KmsDevice::GpuRenderer() const
{
#ifdef VOS_HAVE_KMS_EGL
	if (fGles != NULL)
		return fGles->Renderer()[0] != '\0' ? fGles->Renderer() : "EGL";
#endif
	return "";
}


void
KmsDevice::SetListener(KmsDeviceListener* listener)
{
	pthread_mutex_lock(&fLock);
	fListener = listener;
	pthread_mutex_unlock(&fLock);
}


void
KmsDevice::SetFrontSource(KmsFrontSource* source)
{
	pthread_mutex_lock(&fLock);
	fFrontSource = source;
	pthread_cond_broadcast(&fCondition);
	pthread_mutex_unlock(&fLock);
}


uint32
KmsDevice::_PropertyId(uint32 object, uint32 type, const char* name)
{
	drmModeObjectProperties* properties = drmModeObjectGetProperties(Fd(),
		object, type);
	if (properties == NULL)
		return 0;
	uint32 id = 0;
	for (uint32 i = 0; i < properties->count_props && id == 0; i++) {
		drmModePropertyRes* property = drmModeGetProperty(Fd(),
			properties->props[i]);
		if (property == NULL)
			continue;
		if (strcmp(property->name, name) == 0)
			id = property->prop_id;
		drmModeFreeProperty(property);
	}
	drmModeFreeObjectProperties(properties);
	return id;
}


static uint64
property_value(int fd, uint32 object, uint32 type, const char* name,
	uint64 fallback)
{
	drmModeObjectProperties* properties = drmModeObjectGetProperties(fd,
		object, type);
	if (properties == NULL)
		return fallback;
	uint64 value = fallback;
	for (uint32 i = 0; i < properties->count_props; i++) {
		drmModePropertyRes* property = drmModeGetProperty(fd,
			properties->props[i]);
		if (property == NULL)
			continue;
		if (strcmp(property->name, name) == 0)
			value = properties->prop_values[i];
		drmModeFreeProperty(property);
	}
	drmModeFreeObjectProperties(properties);
	return value;
}


/*!	The CRTCs and their planes, the connectors. */
status_t
KmsDevice::_Probe()
{
	int fd = Fd();
	drmModeRes* resources = drmModeGetResources(fd);
	if (resources == NULL)
		return errno_status();
	if (resources->count_crtcs == 0 || resources->count_connectors == 0) {
		drmModeFreeResources(resources);
		return B_ENTRY_NOT_FOUND;
	}
	fMaxWidth = resources->max_width;
	fMaxHeight = resources->max_height;
	fCrtcs.clear();
	for (int i = 0; i < resources->count_crtcs && i < 32; i++) {
		Crtc crtc;
		memset(&crtc, 0, sizeof(crtc));
		crtc.id = resources->crtcs[i];
		crtc.index = i;
		fCrtcs.push_back(crtc);
	}
	drmModeFreeResources(resources);

	drmModePlaneRes* planes = drmModeGetPlaneResources(fd);
	if (planes != NULL) {
		for (uint32 p = 0; p < planes->count_planes; p++) {
			drmModePlane* plane = drmModeGetPlane(fd, planes->planes[p]);
			if (plane == NULL)
				continue;
			uint64 type = property_value(fd, plane->plane_id,
				DRM_MODE_OBJECT_PLANE, "type", kPlaneOverlay);
			for (size_t c = 0; c < fCrtcs.size(); c++) {
				if ((plane->possible_crtcs & (1u << c)) == 0)
					continue;
				if (type == kPlanePrimary && fCrtcs[c].primaryPlane == 0) {
					fCrtcs[c].primaryPlane = plane->plane_id;
					break;
				}
				if (type == kPlaneCursor && fCrtcs[c].cursorPlane == 0) {
					fCrtcs[c].cursorPlane = plane->plane_id;
					break;
				}
			}
			drmModeFreePlane(plane);
		}
		drmModeFreePlaneResources(planes);
	}

	if (fAtomic) {
		for (size_t c = 0; c < fCrtcs.size(); c++) {
			Crtc& crtc = fCrtcs[c];
			crtc.modeId = _PropertyId(crtc.id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
			crtc.active = _PropertyId(crtc.id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
			if (crtc.primaryPlane != 0) {
				uint32 plane = crtc.primaryPlane;
				uint32 type = DRM_MODE_OBJECT_PLANE;
				crtc.fbId = _PropertyId(plane, type, "FB_ID");
				crtc.crtcId = _PropertyId(plane, type, "CRTC_ID");
				crtc.srcX = _PropertyId(plane, type, "SRC_X");
				crtc.srcY = _PropertyId(plane, type, "SRC_Y");
				crtc.srcW = _PropertyId(plane, type, "SRC_W");
				crtc.srcH = _PropertyId(plane, type, "SRC_H");
				crtc.crtcX = _PropertyId(plane, type, "CRTC_X");
				crtc.crtcY = _PropertyId(plane, type, "CRTC_Y");
				crtc.crtcW = _PropertyId(plane, type, "CRTC_W");
				crtc.crtcH = _PropertyId(plane, type, "CRTC_H");
			}
			if (crtc.cursorPlane != 0) {
				crtc.cursorFbId = _PropertyId(crtc.cursorPlane,
					DRM_MODE_OBJECT_PLANE, "FB_ID");
				crtc.cursorCrtcId = _PropertyId(crtc.cursorPlane,
					DRM_MODE_OBJECT_PLANE, "CRTC_ID");
			}
			if (crtc.modeId == 0 || crtc.active == 0 || crtc.fbId == 0) {
				TRACE("CRTC %u lacks atomic properties: legacy modesetting\n",
					crtc.id);
				fAtomic = false;
			}
		}
		if (!fAtomic)
			drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 0);
	}

	return _ProbeOutputs(true);
}


status_t
KmsDevice::_ProbeOutputs(bool probe)
{
	int fd = Fd();
	drmModeRes* resources = drmModeGetResources(fd);
	if (resources == NULL)
		return errno_status();
	std::vector<Output> outputs;
	for (int i = 0; i < resources->count_connectors
			&& outputs.size() < kKmsMaxOutputs; i++) {
		drmModeConnector* connector = probe
			? drmModeGetConnector(fd, resources->connectors[i])
			: drmModeGetConnectorCurrent(fd, resources->connectors[i]);
		if (connector == NULL)
			continue;
		Output output;
		output.connector = connector->connector_id;
		output.type = connector->connector_type;
		const char* typeName = drmModeGetConnectorTypeName(
			connector->connector_type);
		snprintf(output.name, sizeof(output.name), "%s-%u",
			typeName != NULL ? typeName : "Unknown",
			connector->connector_type_id);
		output.internal = connector->connector_type == DRM_MODE_CONNECTOR_eDP
			|| connector->connector_type == DRM_MODE_CONNECTOR_LVDS
			|| connector->connector_type == DRM_MODE_CONNECTOR_DSI;
		output.widthMM = connector->mmWidth;
		output.heightMM = connector->mmHeight;
		for (int m = 0; m < connector->count_modes; m++)
			output.modes.push_back(connector->modes[m]);
		output.connected = connector->connection == DRM_MODE_CONNECTED
			&& !output.modes.empty();
		output.preferred = output.modes.empty() ? -1 : 0;
		for (size_t m = 0; m < output.modes.size(); m++) {
			if ((output.modes[m].type & DRM_MODE_TYPE_PREFERRED) != 0) {
				output.preferred = m;
				break;
			}
		}
		for (int e = 0; e < connector->count_encoders; e++) {
			drmModeEncoder* encoder = drmModeGetEncoder(fd,
				connector->encoders[e]);
			if (encoder != NULL) {
				output.possibleCrtcs |= encoder->possible_crtcs;
				drmModeFreeEncoder(encoder);
			}
		}
		for (int p = 0; p < connector->count_props; p++) {
			drmModePropertyRes* property = drmModeGetProperty(fd,
				connector->props[p]);
			if (property == NULL)
				continue;
			if (strcmp(property->name, "EDID") == 0
				&& connector->prop_values[p] != 0) {
				drmModePropertyBlobRes* blob = drmModeGetPropertyBlob(fd,
					connector->prop_values[p]);
				if (blob != NULL) {
					output.edidLength = min_c(blob->length,
						(uint32)B_DISPLAY_OUTPUT_EDID_LENGTH);
					memcpy(output.edid, blob->data, output.edidLength);
					drmModeFreePropertyBlob(blob);
				}
			} else if (strcmp(property->name, "DPMS") == 0)
				output.dpmsProperty = property->prop_id;
			else if (strcmp(property->name, "CRTC_ID") == 0)
				output.crtcIdProperty = property->prop_id;
			drmModeFreeProperty(property);
		}
		drmModeFreeConnector(connector);
		outputs.push_back(output);
	}
	drmModeFreeResources(resources);

	// what drives the ones still there stays
	for (size_t i = 0; i < outputs.size(); i++) {
		Output* old = _FindOutput(outputs[i].connector);
		if (old == NULL)
			continue;
		outputs[i].crtcIndex = old->crtcIndex;
		outputs[i].lastBlank = old->lastBlank;
		outputs[i].cursor = old->cursor;
		old->cursor = NULL;
		outputs[i].cursorShown = old->cursorShown;
	}
	for (size_t i = 0; i < fOutputs.size(); i++)
		delete fOutputs[i].cursor;
	fOutputs.swap(outputs);

	// the layouts point at the outputs' modes
	KmsLayout* layouts[2] = { &fCurrent, &fPending };
	for (int l = 0; l < 2; l++) {
		KmsLayout& layout = *layouts[l];
		KmsLayout* fresh = (KmsLayout*)calloc(1, sizeof(KmsLayout));
		if (fresh == NULL)
			continue;
		_FillLayout(*fresh);
		for (uint32 i = 0; i < fresh->count; i++) {
			for (uint32 j = 0; j < layout.count; j++) {
				const KmsLayoutOutput& was = layout.outputs[j];
				if (was.id != fresh->outputs[i].id)
					continue;
				KmsLayoutOutput& output = fresh->outputs[i];
				output.enabled = was.enabled;
				output.mirror = was.mirror;
				output.x = was.x;
				output.y = was.y;
				output.width = was.width;
				output.height = was.height;
				output.scale = was.scale;
				output.renderScale = was.renderScale;
				output.timing = was.timing;
			}
		}
		fresh->width = layout.width;
		fresh->height = layout.height;
		layout = *fresh;
		free(fresh);
	}

	uint32 connected = 0;
	for (size_t i = 0; i < fOutputs.size(); i++) {
		if (fOutputs[i].connected)
			connected++;
	}
	TRACE("%zu outputs, %u connected\n", fOutputs.size(), connected);
	return fOutputs.empty() ? B_ENTRY_NOT_FOUND : B_OK;
}


KmsDevice::Output*
KmsDevice::_FindOutput(uint32 id)
{
	for (size_t i = 0; i < fOutputs.size(); i++) {
		if (fOutputs[i].connector == id)
			return &fOutputs[i];
	}
	return NULL;
}


void
KmsDevice::_FillLayout(KmsLayout& layout) const
{
	memset(&layout, 0, sizeof(layout));
	layout.count = fOutputs.size();
	for (size_t i = 0; i < fOutputs.size(); i++) {
		const Output& source = fOutputs[i];
		KmsLayoutOutput& output = layout.outputs[i];
		output.id = source.connector;
		output.connected = source.connected;
		output.modes = source.modes.empty() ? NULL : &source.modes[0];
		output.modeCount = source.modes.size();
		output.preferred = source.preferred;
		output.scale = output.renderScale = 100;
	}
}


bool
KmsDevice::_OutputsDiffer(const std::vector<Output>& probed) const
{
	if (probed.size() != fOutputs.size())
		return true;
	for (size_t i = 0; i < probed.size(); i++) {
		const Output& a = probed[i];
		const Output& b = fOutputs[i];
		if (a.connector != b.connector || a.connected != b.connected)
			return true;
		if (a.connected && (a.edidLength != b.edidLength
				|| memcmp(a.edid, b.edid, a.edidLength) != 0
				|| a.modes.size() != b.modes.size()))
			return true;
	}
	return false;
}


// #pragma mark - outputs


status_t
KmsDevice::GetDisplayOutputs(display_output** _outputs, uint32* _count)
{
	pthread_mutex_lock(&fLock);
	uint32 count = fOutputs.size();
	display_output* outputs = (display_output*)calloc(max_c(count, 1),
		sizeof(display_output));
	if (outputs == NULL) {
		pthread_mutex_unlock(&fLock);
		return B_NO_MEMORY;
	}
	for (uint32 i = 0; i < count; i++) {
		const Output& source = fOutputs[i];
		display_output& output = outputs[i];
		output.version = B_DISPLAY_OUTPUT_VERSION;
		output.id = source.connector;
		strlcpy(output.name, source.name, sizeof(output.name));
		// the GPU, the CPU or the display engine fits any region
		output.flags = B_DISPLAY_OUTPUT_SCALABLE;
		if (source.connected)
			output.flags |= B_DISPLAY_OUTPUT_CONNECTED;
		if (source.internal)
			output.flags |= B_DISPLAY_OUTPUT_INTERNAL;
		const KmsLayoutOutput* driven = NULL;
		for (uint32 j = 0; j < fCurrent.count; j++) {
			if (fCurrent.outputs[j].id == source.connector
				&& fCurrent.outputs[j].enabled && fModeSet)
				driven = &fCurrent.outputs[j];
		}
		output.scale = output.render_scale = 100;
		if (driven != NULL) {
			output.flags |= B_DISPLAY_OUTPUT_ENABLED;
			if (driven->mirror)
				output.flags |= B_DISPLAY_OUTPUT_MIRROR;
			output.x = driven->x;
			output.y = driven->y;
			output.width = driven->width;
			output.height = driven->height;
			output.scale = driven->scale != 0 ? driven->scale : 100;
			output.render_scale = driven->renderScale != 0
				? driven->renderScale : output.scale;
			kms_mode_to_timing(driven->timing, output.timing);
		} else if (source.preferred >= 0)
			kms_mode_to_timing(source.modes[source.preferred], output.timing);
		if (source.preferred >= 0) {
			kms_mode_to_timing(source.modes[source.preferred],
				output.native_timing);
		}
		output.edid_length = source.edidLength;
		memcpy(output.edid, source.edid, source.edidLength);
	}
	pthread_mutex_unlock(&fLock);
	*_outputs = outputs;
	*_count = count;
	return B_OK;
}


status_t
KmsDevice::GetDisplayOutputModes(uint32 id, display_mode** _modes,
	uint32* _count)
{
	pthread_mutex_lock(&fLock);
	Output* output = _FindOutput(id);
	if (output == NULL) {
		pthread_mutex_unlock(&fLock);
		return B_ENTRY_NOT_FOUND;
	}
	display_mode* modes = (display_mode*)calloc(
		max_c(output->modes.size(), (size_t)1), sizeof(display_mode));
	if (modes == NULL) {
		pthread_mutex_unlock(&fLock);
		return B_NO_MEMORY;
	}
	uint32 count = 0;
	for (size_t m = 0; m < output->modes.size(); m++) {
		if ((output->modes[m].flags & DRM_MODE_FLAG_INTERLACE) != 0)
			continue;
		display_mode& mode = modes[count++];
		kms_mode_to_timing(output->modes[m], mode.timing);
		mode.space = B_RGB32;
		mode.virtual_width = output->modes[m].hdisplay;
		mode.virtual_height = output->modes[m].vdisplay;
		mode.flags = B_HARDWARE_CURSOR | B_PARALLEL_ACCESS | B_DPMS;
	}
	pthread_mutex_unlock(&fLock);
	*_modes = modes;
	*_count = count;
	return B_OK;
}


status_t
KmsDevice::GetEdid(uint32 outputID, uint8* edid, uint32* _length)
{
	pthread_mutex_lock(&fLock);
	const Output* found = NULL;
	if (outputID == 0) {
		const KmsLayoutOutput* primary = fModeSet
			? kms_layout_primary(fCurrent) : NULL;
		if (primary != NULL)
			found = _FindOutput(primary->id);
		for (size_t i = 0; found == NULL && i < fOutputs.size(); i++) {
			if (fOutputs[i].connected && fOutputs[i].edidLength > 0)
				found = &fOutputs[i];
		}
	} else
		found = _FindOutput(outputID);
	status_t status = B_ENTRY_NOT_FOUND;
	if (found != NULL && found->edidLength > 0) {
		uint32 length = min_c(*_length, found->edidLength);
		memcpy(edid, found->edid, length);
		*_length = length;
		status = B_OK;
	}
	pthread_mutex_unlock(&fLock);
	return status;
}


status_t
KmsDevice::SetDisplayLayout(const display_output_config* configs,
	uint32 count, display_mode* _mode)
{
	KmsLayout* layout = (KmsLayout*)malloc(sizeof(KmsLayout));
	if (layout == NULL)
		return B_NO_MEMORY;
	pthread_mutex_lock(&fLock);
	_FillLayout(*layout);
	status_t status = kms_build_layout(*layout, configs, count);
	if (status == B_OK && ((fMaxWidth != 0 && layout->width > fMaxWidth)
			|| (fMaxHeight != 0 && layout->height > fMaxHeight)))
		status = B_BAD_VALUE;
	if (status == B_OK) {
		// CRTCs enough for it?
		std::vector<int32> crtcs;
		status = _AssignCrtcs(*layout, crtcs);
	}
	if (status == B_OK) {
		fPending = *layout;
		fHasPending = true;
		*_mode = kms_layout_mode(*layout);
	}
	pthread_mutex_unlock(&fLock);
	free(layout);
	return status;
}


status_t
KmsDevice::SetMode(display_mode* mode)
{
	if (mode->space != B_RGB32 && mode->space != B_RGBA32)
		return B_BAD_VALUE;
	KmsLayout* layout = (KmsLayout*)malloc(sizeof(KmsLayout));
	if (layout == NULL)
		return B_NO_MEMORY;
	pthread_mutex_lock(&fLock);
	status_t status = B_OK;
	if (fHasPending && kms_is_layout_mode(fPending, *mode)) {
		*layout = fPending;
	} else {
		_FillLayout(*layout);
		kms_default_layout(*layout);
		if (!kms_is_layout_mode(*layout, *mode))
			status = kms_single_mode_layout(*layout, *mode);
	}
	if (status == B_OK)
		status = _ApplyLayout(*layout);
	// applied or not, the pending layout is done with
	fHasPending = false;
	if (status == B_OK)
		*mode = fMode;
	pthread_mutex_unlock(&fLock);
	free(layout);
	return status;
}


status_t
KmsDevice::GetMode(display_mode* mode)
{
	pthread_mutex_lock(&fLock);
	status_t status = B_OK;
	if (fModeSet)
		*mode = fMode;
	else
		status = B_NO_INIT;
	pthread_mutex_unlock(&fLock);
	return status == B_OK ? B_OK : GetPreferredMode(mode);
}


status_t
KmsDevice::GetPreferredMode(display_mode* mode)
{
	pthread_mutex_lock(&fLock);
	status_t status = B_OK;
	if (fHasPending)
		*mode = kms_layout_mode(fPending);
	else {
		KmsLayout* layout = (KmsLayout*)malloc(sizeof(KmsLayout));
		if (layout == NULL)
			status = B_NO_MEMORY;
		else {
			_FillLayout(*layout);
			kms_default_layout(*layout);
			if (layout->width == 0)
				status = B_ERROR;
			else
				*mode = kms_layout_mode(*layout);
			free(layout);
		}
	}
	pthread_mutex_unlock(&fLock);
	return status;
}


status_t
KmsDevice::GetModeList(display_mode** _modes, uint32* _count)
{
	const uint32 max = 256;
	display_mode* list = (display_mode*)calloc(max, sizeof(display_mode));
	KmsLayout* layout = (KmsLayout*)malloc(sizeof(KmsLayout));
	if (list == NULL || layout == NULL) {
		free(list);
		free(layout);
		return B_NO_MEMORY;
	}
	pthread_mutex_lock(&fLock);
	_FillLayout(*layout);
	uint32 count = kms_mode_list(fHasPending ? &fPending : NULL,
		fModeSet ? &fMode : NULL, *layout, list, max);
	pthread_mutex_unlock(&fLock);
	free(layout);
	if (count == 0) {
		free(list);
		return B_ERROR;
	}
	*_modes = list;
	*_count = count;
	return B_OK;
}


// #pragma mark - mode setting


/*!	A monitor keeps the CRTC it has (moving it is a mode set on a monitor
	that did not change), the others get free ones. \a crtcs gets a CRTC
	index per layout output, -1 for those not enabled. */
status_t
KmsDevice::_AssignCrtcs(KmsLayout& layout, std::vector<int32>& crtcs)
{
	crtcs.assign(layout.count, -1);
	uint32 used = 0;
	for (uint32 i = 0; i < layout.count; i++) {
		const KmsLayoutOutput& output = layout.outputs[i];
		const Output* device = _FindOutput(output.id);
		if (!output.enabled || device == NULL)
			continue;
		int32 c = device->crtcIndex;
		if (c >= 0 && (device->possibleCrtcs & (1u << c)) != 0
			&& (used & (1u << c)) == 0
			&& (!fAtomic || fCrtcs[c].primaryPlane != 0)) {
			crtcs[i] = c;
			used |= 1u << c;
		}
	}
	for (uint32 i = 0; i < layout.count; i++) {
		const KmsLayoutOutput& output = layout.outputs[i];
		const Output* device = _FindOutput(output.id);
		if (!output.enabled || crtcs[i] >= 0)
			continue;
		if (device == NULL)
			return B_ENTRY_NOT_FOUND;
		for (uint32 c = 0; c < fCrtcs.size(); c++) {
			if ((device->possibleCrtcs & (1u << c)) != 0
				&& (used & (1u << c)) == 0
				&& (!fAtomic || fCrtcs[c].primaryPlane != 0)) {
				crtcs[i] = c;
				used |= 1u << c;
				break;
			}
		}
		if (crtcs[i] < 0)
			return B_NOT_SUPPORTED;
	}
	return B_OK;
}


KmsBuffer*
KmsDevice::_CreateScanoutBuffer(uint32 width, uint32 height)
{
	KmsBuffer* buffer = NULL;
#ifdef VOS_HAVE_KMS_GBM
	if (fUseGbmBuffers) {
		buffer = KmsBuffer::CreateGbm(Fd(), fGbm, width, height,
			GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
		if (buffer == NULL && !fForceBuffers)
			fUseGbmBuffers = false;
	}
#endif
	if (buffer == NULL && fDumb) {
		buffer = KmsBuffer::CreateDumb(Fd(), width, height,
			DRM_FORMAT_XRGB8888);
	}
	return buffer;
}


/*!	The scan-outs of a layout: one per monitor that is not a mirror, which
	its mirrors share where they can show the same buffer. A region the
	size of its monitor's mode is copied one to one; else the display
	engine scales it (\a allowPlaneScaling, atomic only) or the buffer is
	the mode's size and the region is resampled into it (GPU or CPU). */
status_t
KmsDevice::_BuildScanouts(const KmsLayout& layout,
	const std::vector<int32>& crtcs, bool allowPlaneScaling,
	std::vector<Scanout*>& scanouts)
{
	std::vector<Scanout*> byOutput(layout.count, (Scanout*)NULL);
	for (int pass = 0; pass < 2; pass++) {
		for (uint32 i = 0; i < layout.count; i++) {
			const KmsLayoutOutput& output = layout.outputs[i];
			if (!output.enabled || output.mirror != (pass == 1))
				continue;
			KmsRect region = kms_region(output);
			KmsRect destination = kms_destination(output);
			KmsRect mode(0, 0, output.timing.hdisplay,
				output.timing.vdisplay);
			if (output.mirror) {
				// the source's scan-out, when it shows the same buffer
				Scanout* source = NULL;
				for (uint32 j = 0; j < layout.count; j++) {
					const KmsLayoutOutput& other = layout.outputs[j];
					if (other.enabled && !other.mirror && other.x == output.x
						&& other.y == output.y)
						source = byOutput[j];
				}
				bool sameSize = destination.width == region.width
					&& destination.height == region.height;
				if (source != NULL && !source->resample
					&& source->width == (uint32)region.width
					&& source->height == (uint32)region.height
					&& ((fAtomic && (sameSize || allowPlaneScaling))
						|| (!fAtomic && sameSize && destination == mode))) {
					source->outputs.push_back(output.id);
					byOutput[i] = source;
					continue;
				}
			}
			Scanout* scanout = new(std::nothrow) Scanout;
			if (scanout == NULL)
				return B_NO_MEMORY;
			scanout->index = scanouts.size();
			scanouts.push_back(scanout);
			byOutput[i] = scanout;
			scanout->outputs.push_back(output.id);
			scanout->region = region;
			bool direct = region.width == mode.width
				&& region.height == mode.height && destination == mode;
			if (direct || (fAtomic && allowPlaneScaling)) {
				scanout->width = region.width;
				scanout->height = region.height;
				scanout->destination = KmsRect(0, 0, region.width,
					region.height);
				scanout->planeScaled = !direct;
			} else {
				scanout->width = mode.width;
				scanout->height = mode.height;
				scanout->destination = destination;
				scanout->resample = true;
				status_t status = scanout->scaler.SetTo(region.width,
					region.height, destination.width, destination.height);
				if (status != B_OK)
					return status;
			}
			scanout->full = true;
		}
	}

	for (size_t s = 0; s < scanouts.size(); s++) {
		Scanout* scanout = scanouts[s];
#ifdef VOS_HAVE_KMS_EGL
		if (UsesGpu()) {
			status_t error;
			scanout->gl = fGles->CreateTarget(scanout->width, scanout->height,
				scanout->region.width, scanout->region.height,
				scanout->destination, &error);
			if (scanout->gl != NULL && fGles->IsSoftware()) {
				const char* gpu = getenv("VOS_KMS_GPU");
				if (gpu == NULL || strcmp(gpu, "1") != 0) {
					TRACE("GPU pass on %s: the CPU path is used instead\n",
						fGles->Renderer());
					fGles->DestroyTarget(scanout->gl);
					scanout->gl = NULL;
					fGlesFailed = true;
				}
			}
			if (scanout->gl != NULL) {
				// the first frame, black, to set the mode with
				std::vector<KmsRect> none;
				gbm_bo* bo = fGles->Render(scanout->gl, NULL, 0, none, true);
				KmsBuffer* buffer = bo != NULL
					? KmsBuffer::ForSurfaceBo(Fd(), bo) : NULL;
				if (buffer == NULL) {
					if (bo != NULL)
						fGles->ReleaseFrame(scanout->gl, bo);
					fGles->DestroyTarget(scanout->gl);
					scanout->gl = NULL;
				} else {
					scanout->glBuffers.push_back(buffer);
					scanout->shown = buffer;
					continue;
				}
			}
			if (!fGlesFailed)
				TRACE("no GPU pass for a %ux%u scan-out: CPU\n",
					scanout->width, scanout->height);
		}
#endif
		for (int32 i = 0; i < fSlots; i++) {
			KmsBuffer* buffer = _CreateScanoutBuffer(scanout->width,
				scanout->height);
			if (buffer == NULL) {
				TRACE("no %ux%u scan-out buffer\n", scanout->width,
					scanout->height);
				return B_NO_MEMORY;
			}
			scanout->buffers.push_back(buffer);
			scanout->stale[i].clear();
			scanout->stale[i].push_back(KmsRect(0, 0, scanout->width,
				scanout->height));
		}
		scanout->shown = scanout->buffers[0];
	}
	return B_OK;
}


void
KmsDevice::_DestroyScanout(Scanout* scanout)
{
	if (scanout == NULL)
		return;
	for (size_t i = 0; i < scanout->buffers.size(); i++)
		delete scanout->buffers[i];
#ifdef VOS_HAVE_KMS_EGL
	for (size_t i = 0; i < scanout->glBuffers.size(); i++) {
		gbm_bo* bo = scanout->glBuffers[i]->Bo();
		delete scanout->glBuffers[i];
		fGles->ReleaseFrame(scanout->gl, bo);
	}
	for (size_t i = 0; i < scanout->release.size(); i++)
		fGles->ReleaseFrame(scanout->gl, scanout->release[i]);
	if (scanout->gl != NULL)
		fGles->DestroyTarget(scanout->gl);
#endif
	delete scanout;
}


void
KmsDevice::_DestroyScanouts(std::vector<Scanout*>& scanouts)
{
	for (size_t i = 0; i < scanouts.size(); i++)
		_DestroyScanout(scanouts[i]);
	scanouts.clear();
}


status_t
KmsDevice::_CommitModeset(const KmsLayout& layout,
	const std::vector<int32>& crtcs, std::vector<Scanout*>& scanouts,
	bool testOnly)
{
	int fd = Fd();
	uint32 used = 0;
	for (uint32 i = 0; i < layout.count; i++) {
		if (crtcs[i] >= 0)
			used |= 1u << crtcs[i];
	}

	if (!fAtomic) {
		if (testOnly)
			return B_OK;
		status_t status = B_OK;
		for (uint32 i = 0; i < layout.count && status == B_OK; i++) {
			const KmsLayoutOutput& output = layout.outputs[i];
			if (!output.enabled)
				continue;
			Scanout* scanout = NULL;
			for (size_t s = 0; s < scanouts.size() && scanout == NULL; s++) {
				for (size_t o = 0; o < scanouts[s]->outputs.size(); o++) {
					if (scanouts[s]->outputs[o] == output.id)
						scanout = scanouts[s];
				}
			}
			uint32 connector = output.id;
			drmModeModeInfo mode = output.timing;
			if (scanout == NULL || drmModeSetCrtc(fd, fCrtcs[crtcs[i]].id,
					scanout->shown->FramebufferId(), 0, 0, &connector, 1,
					&mode) != 0) {
				status = errno_status();
				TRACE("setting the mode of %s failed: %s\n",
					_FindOutput(output.id) != NULL
						? _FindOutput(output.id)->name : "?",
					strerror(errno));
			}
		}
		for (uint32 c = 0; status == B_OK && c < fCrtcs.size(); c++) {
			if ((used & (1u << c)) == 0)
				drmModeSetCrtc(fd, fCrtcs[c].id, 0, 0, 0, NULL, 0, NULL);
		}
		return status;
	}

	drmModeAtomicReq* request = drmModeAtomicAlloc();
	if (request == NULL)
		return B_NO_MEMORY;
	std::vector<uint32> blobs(fCrtcs.size(), 0);
	status_t status = B_OK;
	for (size_t o = 0; o < fOutputs.size(); o++) {
		const Output& device = fOutputs[o];
		const KmsLayoutOutput* output = NULL;
		int32 crtcIndex = -1;
		for (uint32 i = 0; i < layout.count; i++) {
			if (layout.outputs[i].id == device.connector
				&& layout.outputs[i].enabled) {
				output = &layout.outputs[i];
				crtcIndex = crtcs[i];
			}
		}
		if (output == NULL || crtcIndex < 0) {
			if (device.crtcIdProperty != 0) {
				drmModeAtomicAddProperty(request, device.connector,
					device.crtcIdProperty, 0);
			}
			continue;
		}
		Scanout* scanout = NULL;
		for (size_t s = 0; s < scanouts.size() && scanout == NULL; s++) {
			for (size_t k = 0; k < scanouts[s]->outputs.size(); k++) {
				if (scanouts[s]->outputs[k] == device.connector)
					scanout = scanouts[s];
			}
		}
		if (scanout == NULL) {
			status = B_ERROR;
			break;
		}
		Crtc& crtc = fCrtcs[crtcIndex];
		drmModeModeInfo mode = output->timing;
		if (drmModeCreatePropertyBlob(fd, &mode, sizeof(mode),
				&blobs[crtcIndex]) != 0) {
			status = errno_status();
			break;
		}
		KmsRect onCrtc = scanout->resample
			? KmsRect(0, 0, scanout->width, scanout->height)
			: kms_destination(*output);
		drmModeAtomicAddProperty(request, device.connector,
			device.crtcIdProperty, crtc.id);
		drmModeAtomicAddProperty(request, crtc.id, crtc.modeId,
			blobs[crtcIndex]);
		drmModeAtomicAddProperty(request, crtc.id, crtc.active,
			fDPMSMode == B_DPMS_ON ? 1 : 0);
		drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.fbId,
			scanout->shown->FramebufferId());
		drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.crtcId,
			crtc.id);
		drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.srcX, 0);
		drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.srcY, 0);
		drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.srcW,
			(uint64)scanout->width << 16);
		drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.srcH,
			(uint64)scanout->height << 16);
		drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.crtcX,
			onCrtc.x);
		drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.crtcY,
			onCrtc.y);
		drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.crtcW,
			onCrtc.width);
		drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.crtcH,
			onCrtc.height);
	}
	// the cursors go, and come back after the mode set (_ApplyCursor())
	for (uint32 c = 0; status == B_OK && c < fCrtcs.size(); c++) {
		const Crtc& crtc = fCrtcs[c];
		if (crtc.cursorPlane != 0 && crtc.cursorFbId != 0) {
			drmModeAtomicAddProperty(request, crtc.cursorPlane,
				crtc.cursorFbId, 0);
			drmModeAtomicAddProperty(request, crtc.cursorPlane,
				crtc.cursorCrtcId, 0);
		}
		if ((used & (1u << c)) != 0)
			continue;
		drmModeAtomicAddProperty(request, crtc.id, crtc.active, 0);
		drmModeAtomicAddProperty(request, crtc.id, crtc.modeId, 0);
		if (crtc.primaryPlane != 0) {
			drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.fbId, 0);
			drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.crtcId,
				0);
		}
	}
	if (status == B_OK) {
		if (drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET
				| (testOnly ? DRM_MODE_ATOMIC_TEST_ONLY : 0), NULL) != 0)
			status = errno_status();
	}
	drmModeAtomicFree(request);

	if (status != B_OK || testOnly) {
		for (size_t c = 0; c < blobs.size(); c++) {
			if (blobs[c] != 0)
				drmModeDestroyPropertyBlob(fd, blobs[c]);
		}
		return status;
	}
	for (size_t c = 0; c < fCrtcs.size(); c++) {
		if (fCrtcs[c].modeBlob != 0)
			drmModeDestroyPropertyBlob(fd, fCrtcs[c].modeBlob);
		fCrtcs[c].modeBlob = blobs[c];
	}
	return B_OK;
}


/*!	Drives the monitors as \a layout says. Locked; app_server holds the
	frame buffer locked for writing, so no copy reads it meanwhile. */
status_t
KmsDevice::_ApplyLayout(KmsLayout& layout)
{
	if (layout.width == 0 || layout.height == 0)
		return B_BAD_VALUE;
	if (!fSession.IsActive())
		return B_NOT_ALLOWED;

	std::vector<int32> crtcs;
	status_t status = _AssignCrtcs(layout, crtcs);
	if (status != B_OK)
		return status;

	_Drain(500000);

	// the display engine's scaling first, if asked for and accepted
	const char* planeScaling = getenv("VOS_KMS_PLANE_SCALING");
	bool tryPlaneScaling = fAtomic
		&& (planeScaling == NULL || strcmp(planeScaling, "0") != 0);
	std::vector<Scanout*> scanouts;
	bool scaled = false;
	if (tryPlaneScaling) {
		status = _BuildScanouts(layout, crtcs, true, scanouts);
		for (size_t s = 0; s < scanouts.size(); s++)
			scaled |= scanouts[s]->planeScaled;
		if (status == B_OK && scaled)
			status = _CommitModeset(layout, crtcs, scanouts, true);
		if (status != B_OK || !scaled) {
			if (scaled) {
				TRACE("the display engine does not scale this layout (%s):"
					" resampled\n", strerror(status));
			}
			_DestroyScanouts(scanouts);
			scaled = false;
		}
	}
	if (scanouts.empty()) {
		status = _BuildScanouts(layout, crtcs, false, scanouts);
		if (status == B_OK)
			status = _CommitModeset(layout, crtcs, scanouts, true);
	}
	if (status == B_OK)
		status = _CommitModeset(layout, crtcs, scanouts, false);
	if (status != B_OK) {
		TRACE("cannot set the %ux%u layout: %s\n", layout.width,
			layout.height, strerror(status));
		_DestroyScanouts(scanouts);
		return status;
	}

	// on screen: the old buffers are not shown any more
	_DestroyScanouts(fScanouts);
	fScanouts.swap(scanouts);
	fGeneration++;
	for (size_t o = 0; o < fOutputs.size(); o++) {
		fOutputs[o].crtcIndex = -1;
		fOutputs[o].blankArmed = false;
		fOutputs[o].cursorShown = false;
	}
	for (uint32 i = 0; i < layout.count; i++) {
		Output* output = _FindOutput(layout.outputs[i].id);
		if (output != NULL && layout.outputs[i].enabled)
			output->crtcIndex = crtcs[i];
	}
	fCurrent = layout;
	fModeSet = true;
	fMode = kms_layout_mode(layout);
	for (size_t s = 0; s < fScanouts.size(); s++) {
		Scanout* scanout = fScanouts[s];
		TRACE("scan-out %zu: region %d,%d %dx%d -> %ux%u buffer%s%s, %s, "
			"%zu output(s)\n", s, scanout->region.x, scanout->region.y,
			scanout->region.width, scanout->region.height, scanout->width,
			scanout->height, scanout->resample ? ", resampled" : "",
			scanout->planeScaled ? ", plane scaled" : "",
			scanout->gl != NULL ? "GPU" : "CPU", scanout->outputs.size());
	}
	for (uint32 i = 0; i < layout.count; i++) {
		const KmsLayoutOutput& output = layout.outputs[i];
		if (!output.enabled)
			continue;
		TRACE("%s: %ux%u@%u, region %d,%d %ux%u%s, CRTC %d, scale %u%%, "
			"render scale %u%%\n", _FindOutput(output.id)->name,
			output.timing.hdisplay, output.timing.vdisplay,
			output.timing.vrefresh, output.x, output.y, output.width,
			output.height, output.mirror ? " (mirror)" : "", crtcs[i],
			output.scale, output.renderScale);
	}

	if (!fSplashQuit) {
		// the first picture of ours is up: the boot splash can go
		fSplashQuit = true;
		vos_splash_handoff();
	}
	for (size_t o = 0; o < fOutputs.size(); o++) {
		if (fOutputs[o].crtcIndex >= 0) {
			_ApplyCursor(fOutputs[o]);
			_ApplyGamma(fOutputs[o]);
		}
	}
	_DamageLocked(KmsRect(0, 0, layout.width, layout.height));
	return B_OK;
}


/*!	Waits for the flips under way and the copy that may be running; on the
	event thread the events are taken here. Locked. */
status_t
KmsDevice::_Drain(bigtime_t timeout)
{
	bigtime_t deadline = kms_monotonic_time() + timeout;
	bool onEventThread = fEventThreadRunning
		&& pthread_equal(pthread_self(), fEventThread);
	while (true) {
		bool busy = fRendering;
		for (size_t s = 0; s < fScanouts.size(); s++)
			busy |= fScanouts[s]->pending != NULL;
		if (!busy)
			return B_OK;
		bigtime_t now = kms_monotonic_time();
		if (now >= deadline)
			break;
		if (onEventThread && !fRendering) {
			pthread_mutex_unlock(&fLock);
			struct pollfd pfd = { Fd(), POLLIN, 0 };
			if (poll(&pfd, 1, 16) > 0) {
				drmEventContext context;
				memset(&context, 0, sizeof(context));
				context.version = 3;
				context.vblank_handler = _BlankHandler;
				context.page_flip_handler2 = _FlipHandler;
				drmHandleEvent(Fd(), &context);
			}
			pthread_mutex_lock(&fLock);
			continue;
		}
		struct timespec until;
		clock_gettime(CLOCK_REALTIME, &until);
		bigtime_t wait = min_c(deadline - now, (bigtime_t)20000);
		until.tv_nsec += wait * 1000;
		until.tv_sec += until.tv_nsec / 1000000000;
		until.tv_nsec %= 1000000000;
		pthread_cond_timedwait(&fCondition, &fLock, &until);
	}
	// given up on: a late event must not touch what replaces them
	TRACE("flips did not complete in time; abandoned\n");
	fGeneration++;
	for (size_t s = 0; s < fScanouts.size(); s++) {
		Scanout* scanout = fScanouts[s];
		if (scanout->pending != NULL) {
			scanout->shown = scanout->pending;
			scanout->pending = NULL;
			scanout->pendingEvents = 0;
		}
	}
	return B_TIMED_OUT;
}


// #pragma mark - presentation


void
KmsDevice::_DamageLocked(const KmsRect& area)
{
	for (size_t s = 0; s < fScanouts.size(); s++) {
		Scanout* scanout = fScanouts[s];
		KmsRect inside = area.Intersect(scanout->region);
		if (inside.IsEmpty())
			continue;
		std::vector<KmsRect>& damage = scanout->damage;
		// merge into an overlapping one, keep the list short
		bool merged = false;
		for (size_t i = 0; i < damage.size() && !merged; i++) {
			if (!damage[i].Intersect(inside).IsEmpty()
				|| damage[i].Union(inside) == damage[i]) {
				damage[i] = damage[i].Union(inside);
				merged = true;
			}
		}
		if (!merged)
			damage.push_back(inside);
		if (damage.size() > kMaxDamageRects) {
			KmsRect all;
			for (size_t i = 0; i < damage.size(); i++)
				all = all.Union(damage[i]);
			damage.clear();
			damage.push_back(all);
		}
	}
	pthread_cond_broadcast(&fCondition);
}


void
KmsDevice::Damage(const KmsRect& area)
{
	if (area.IsEmpty())
		return;
	pthread_mutex_lock(&fLock);
	_DamageLocked(area);
	pthread_mutex_unlock(&fLock);
}


void
KmsDevice::DamageAll()
{
	pthread_mutex_lock(&fLock);
	for (size_t s = 0; s < fScanouts.size(); s++)
		fScanouts[s]->full = true;
	pthread_cond_broadcast(&fCondition);
	pthread_mutex_unlock(&fLock);
}


bool
KmsDevice::_CanRender(Scanout* scanout) const
{
	if (!fSession.IsActive() || fDPMSMode != B_DPMS_ON || scanout->rendering)
		return false;
	if (scanout->damage.empty() && !scanout->full)
		return false;
	if (scanout->gl != NULL) {
		// one frame waiting at most; a newer one replaces it
		return true;
	}
	return _FreeSlot(scanout) >= 0;
}


int32
KmsDevice::_FreeSlot(Scanout* scanout) const
{
	for (size_t i = 0; i < scanout->buffers.size(); i++) {
		if (scanout->buffers[i] == scanout->queued)
			return i;
	}
	for (size_t i = 0; i < scanout->buffers.size(); i++) {
		KmsBuffer* buffer = scanout->buffers[i];
		if (buffer != scanout->shown && buffer != scanout->pending)
			return i;
	}
	return -1;
}


/*!	Copies \a damage (frame buffer pixels) into the scan-out's buffer in
	\a slot, with what that buffer missed since it was last drawn. Not
	locked: the buffer is the present thread's until it is queued. */
void
KmsDevice::_Render(Scanout* scanout, const KmsFrontView& front,
	std::vector<KmsRect>& damage, int32 slot, bool full)
{
	KmsRect frontBounds(0, 0, front.width, front.height);
	KmsRect region = scanout->region.Intersect(frontBounds);
	const uint8* source = front.bits + (size_t)scanout->region.y
		* front.bytesPerRow + (size_t)scanout->region.x * 4;
	bool complete = region == scanout->region;

#ifdef VOS_HAVE_KMS_EGL
	if (scanout->gl != NULL) {
		std::vector<KmsRect> local;
		for (size_t i = 0; i < damage.size(); i++) {
			KmsRect rect = damage[i].Intersect(region);
			if (rect.IsEmpty())
				continue;
			local.push_back(KmsRect(rect.x - scanout->region.x,
				rect.y - scanout->region.y, rect.width, rect.height));
		}
		gbm_bo* bo = fGles->Render(scanout->gl, complete ? source : NULL,
			front.bytesPerRow, local, full || !complete);
		KmsBuffer* buffer = NULL;
		if (bo != NULL) {
			for (size_t i = 0; i < scanout->glBuffers.size(); i++) {
				if (scanout->glBuffers[i]->Bo() == bo)
					buffer = scanout->glBuffers[i];
			}
			if (buffer == NULL) {
				buffer = KmsBuffer::ForSurfaceBo(Fd(), bo);
				if (buffer != NULL)
					scanout->glBuffers.push_back(buffer);
				else
					fGles->ReleaseFrame(scanout->gl, bo);
			}
		}
		// handed over in damage[0]'s place: see _PresentThread()
		damage.clear();
		if (buffer != NULL) {
			damage.push_back(KmsRect(-1, -1, 0, 0));
			scanout->stale[0].clear();
			scanout->stale[0].push_back(KmsRect());
			// the frame, kept for the present thread
			scanout->release.push_back(NULL);
			scanout->release.back() = (gbm_bo*)buffer;
		}
		return;
	}
#endif

	KmsBuffer* buffer = scanout->buffers[slot];
	KmsRect bufferBounds(0, 0, scanout->width, scanout->height);

	// what changed, in the buffer's pixels
	std::vector<KmsRect> changed;
	if (full) {
		changed.push_back(bufferBounds);
	} else {
		for (size_t i = 0; i < damage.size(); i++) {
			KmsRect rect = damage[i].Intersect(scanout->region);
			if (rect.IsEmpty())
				continue;
			if (scanout->resample) {
				changed.push_back(kms_map_damage(rect, scanout->region,
					scanout->destination));
			} else {
				changed.push_back(KmsRect(rect.x - scanout->region.x,
					rect.y - scanout->region.y, rect.width, rect.height));
			}
		}
	}
	// what this buffer missed, too
	std::vector<KmsRect> paint = changed;
	for (size_t i = 0; i < scanout->stale[slot].size(); i++)
		paint.push_back(scanout->stale[slot][i]);

	uint8* bits = buffer->Map();
	if (bits == NULL) {
		changed.clear();
		damage.swap(changed);
		return;
	}
	uint32 stride = buffer->Stride();
	for (size_t i = 0; i < paint.size(); i++) {
		KmsRect rect = paint[i].Intersect(bufferBounds);
		if (rect.IsEmpty())
			continue;
		if (scanout->resample) {
			// black around a letterboxed mirror
			KmsRect inside = rect.Intersect(scanout->destination);
			if (!(inside == rect)) {
				// the parts outside: rows above and below, sides
				KmsRect parts[4] = {
					KmsRect(rect.x, rect.y, rect.width,
						max_c(0, min_c(rect.Bottom(),
							scanout->destination.y) - rect.y)),
					KmsRect(rect.x, max_c(rect.y,
							scanout->destination.Bottom()), rect.width,
						rect.Bottom() - max_c(rect.y,
							scanout->destination.Bottom())),
					KmsRect(rect.x, rect.y, max_c(0, min_c(rect.Right(),
						scanout->destination.x) - rect.x), rect.height),
					KmsRect(max_c(rect.x, scanout->destination.Right()),
						rect.y, rect.Right() - max_c(rect.x,
							scanout->destination.Right()), rect.height)
				};
				for (int p = 0; p < 4; p++) {
					if (!parts[p].IsEmpty())
						kms_fill_rect(bits, stride, parts[p], 0xff000000);
				}
			}
			if (inside.IsEmpty())
				continue;
			if (!complete) {
				kms_fill_rect(bits, stride, inside, 0xff000000);
				continue;
			}
			KmsRect area(inside.x - scanout->destination.x,
				inside.y - scanout->destination.y, inside.width,
				inside.height);
			scanout->scaler.Scale(source, front.bytesPerRow,
				bits + (size_t)scanout->destination.y * stride
					+ (size_t)scanout->destination.x * 4,
				stride, area, true);
		} else {
			KmsRect from(rect.x + scanout->region.x,
				rect.y + scanout->region.y, rect.width, rect.height);
			KmsRect available = from.Intersect(frontBounds);
			if (!(available == from))
				kms_fill_rect(bits, stride, rect, 0xff000000);
			if (available.IsEmpty())
				continue;
			kms_copy_rect(front.bits, front.bytesPerRow, bits, stride,
				available, available.x - scanout->region.x,
				available.y - scanout->region.y, true);
		}
	}
	buffer->Unmap();
	damage.swap(changed);
}


/*!	Flips the queued buffer. Locked. */
status_t
KmsDevice::_Commit(Scanout* scanout)
{
	if (scanout->queued == NULL || scanout->pending != NULL
		|| !fSession.IsActive() || fDPMSMode != B_DPMS_ON)
		return B_OK;
	KmsBuffer* buffer = scanout->queued;
	uint64 data = event_data(kEventFlip, scanout->index, fGeneration);
	int fd = Fd();
	uint32 events = 0;
	status_t status = B_OK;

	if (fAtomic && !fFlipBroken) {
		drmModeAtomicReq* request = drmModeAtomicAlloc();
		if (request == NULL)
			return B_NO_MEMORY;
		for (size_t o = 0; o < scanout->outputs.size(); o++) {
			Output* output = _FindOutput(scanout->outputs[o]);
			if (output == NULL || output->crtcIndex < 0)
				continue;
			const Crtc& crtc = fCrtcs[output->crtcIndex];
			drmModeAtomicAddProperty(request, crtc.primaryPlane, crtc.fbId,
				buffer->FramebufferId());
			events++;
		}
		if (events > 0 && drmModeAtomicCommit(fd, request,
				DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT,
				(void*)(uintptr_t)data) != 0)
			status = errno_status();
		drmModeAtomicFree(request);
	} else if (!fFlipBroken) {
		for (size_t o = 0; o < scanout->outputs.size() && status == B_OK;
				o++) {
			Output* output = _FindOutput(scanout->outputs[o]);
			if (output == NULL || output->crtcIndex < 0)
				continue;
			if (drmModePageFlip(fd, fCrtcs[output->crtcIndex].id,
					buffer->FramebufferId(), DRM_MODE_PAGE_FLIP_EVENT,
					(void*)(uintptr_t)data) != 0) {
				status = errno_status();
				// the CRTCs that flipped already finish as one
				if (events > 0)
					status = B_OK;
				break;
			}
			events++;
		}
	}

	if (status == -EBUSY)
		return status;	// a flip of another scan-out: at its event
	if (status != B_OK || fFlipBroken) {
		if (!fFlipBroken && drm_flip_failure_counts(-status, false,
				&fFlipFailures)) {
			TRACE("page flips do not work here (%s): the mode is set for "
				"each frame instead (may tear)\n", strerror(-status));
			fFlipBroken = true;
		}
		if (!fFlipBroken)
			return status;
		// the fallback: set the buffer, at once
		for (size_t o = 0; o < scanout->outputs.size(); o++) {
			Output* output = _FindOutput(scanout->outputs[o]);
			if (output == NULL || output->crtcIndex < 0)
				continue;
			const KmsLayoutOutput* driven = NULL;
			for (uint32 i = 0; i < fCurrent.count; i++) {
				if (fCurrent.outputs[i].id == output->connector)
					driven = &fCurrent.outputs[i];
			}
			if (driven == NULL)
				continue;
			uint32 connector = output->connector;
			drmModeModeInfo mode = driven->timing;
			drmModeSetCrtc(fd, fCrtcs[output->crtcIndex].id,
				buffer->FramebufferId(), 0, 0, &connector, 1, &mode);
			drmModeDirtyFB(fd, buffer->FramebufferId(), NULL, 0);
		}
		scanout->pending = buffer;
		scanout->queued = NULL;
		scanout->pendingEvents = 1;
		_FlipDone(scanout, 0, kms_monotonic_time());
		return B_OK;
	}
	fFlipFailures = 0;
	scanout->pending = buffer;
	scanout->queued = NULL;
	scanout->pendingEvents = events;
	return B_OK;
}


/*!	A CRTC showing the scan-out's pending buffer has flipped. Locked. */
void
KmsDevice::_FlipDone(Scanout* scanout, uint32 crtcId, bigtime_t when)
{
	for (size_t o = 0; o < scanout->outputs.size(); o++) {
		Output* output = _FindOutput(scanout->outputs[o]);
		if (output == NULL || output->crtcIndex < 0
			|| (crtcId != 0 && fCrtcs[output->crtcIndex].id != crtcId))
			continue;
		output->lastBlank = when;
		if (!output->blankArmed && fListener != NULL)
			fListener->KmsRetrace(output->connector, when);
	}
	if (scanout->pendingEvents > 0)
		scanout->pendingEvents--;
	if (scanout->pendingEvents > 0)
		return;
	KmsBuffer* old = scanout->shown;
	scanout->shown = scanout->pending;
	scanout->pending = NULL;
	fFlips++;
	if (scanout->gl != NULL && old != NULL && old != scanout->shown) {
		// the surface gets the buffer back on the present thread
		scanout->release.push_back((gbm_bo*)old);
	}
	_Commit(scanout);
	pthread_cond_broadcast(&fCondition);
}


/*static*/ void
KmsDevice::_FlipHandler(int fd, unsigned int sequence, unsigned int seconds,
	unsigned int microseconds, unsigned int crtcId, void* data)
{
	KmsDevice* device = sEventDevice;
	if (device == NULL)
		return;
	uint64 value = (uint64)(uintptr_t)data;
	uint32 index = (value >> 8) & 0xff;
	uint32 generation = value >> 16;
	bigtime_t when = (bigtime_t)seconds * 1000000 + microseconds;
	pthread_mutex_lock(&device->fLock);
	if (generation == device->fGeneration
		&& index < device->fScanouts.size()
		&& device->fScanouts[index]->pending != NULL)
		device->_FlipDone(device->fScanouts[index], crtcId, when);
	pthread_mutex_unlock(&device->fLock);
}


/*static*/ void
KmsDevice::_BlankHandler(int fd, unsigned int sequence, unsigned int seconds,
	unsigned int microseconds, void* data)
{
	KmsDevice* device = sEventDevice;
	if (device == NULL)
		return;
	uint64 value = (uint64)(uintptr_t)data;
	uint32 index = (value >> 8) & 0xff;
	uint32 generation = value >> 16;
	bigtime_t when = (bigtime_t)seconds * 1000000 + microseconds;
	pthread_mutex_lock(&device->fLock);
	if (index < device->fOutputs.size()) {
		Output& output = device->fOutputs[index];
		output.blankArmed = false;
		if (generation == device->fBlankGeneration) {
			output.lastBlank = when;
			if (device->fListener != NULL)
				device->fListener->KmsRetrace(output.connector, when);
		}
	}
	pthread_cond_broadcast(&device->fCondition);
	pthread_mutex_unlock(&device->fLock);
}


/*!	Vertical blank events for the outputs while someone waits for them.
	Locked. */
void
KmsDevice::_ArmBlanks()
{
	if (!fSession.IsActive() || fDPMSMode != B_DPMS_ON)
		return;
	if (kms_monotonic_time() >= fRetraceWantedUntil)
		return;
	for (size_t o = 0; o < fOutputs.size(); o++) {
		Output& output = fOutputs[o];
		if (output.crtcIndex < 0 || output.blankArmed)
			continue;
		drmVBlank blank;
		memset(&blank, 0, sizeof(blank));
		blank.request.type = (drmVBlankSeqType)(DRM_VBLANK_RELATIVE
			| DRM_VBLANK_EVENT | vblank_crtc_bits(output.crtcIndex));
		blank.request.sequence = 1;
		blank.request.signal = (unsigned long)event_data(kEventBlank, o,
			fBlankGeneration);
		if (drmWaitVBlank(Fd(), &blank) == 0)
			output.blankArmed = true;
	}
}


void
KmsDevice::WantRetrace()
{
	pthread_mutex_lock(&fLock);
	bigtime_t now = kms_monotonic_time();
	bool wake = fRetraceWantedUntil < now + 500000;
	fRetraceWantedUntil = now + 1000000;
	pthread_mutex_unlock(&fLock);
	if (wake)
		_Wake();
}


status_t
KmsDevice::WaitForRetrace(uint32 outputID, bigtime_t timeout)
{
	WantRetrace();
	pthread_mutex_lock(&fLock);
	Output* output = _FindOutput(outputID);
	if (output == NULL || output->crtcIndex < 0) {
		pthread_mutex_unlock(&fLock);
		return B_ENTRY_NOT_FOUND;
	}
	bigtime_t last = output->lastBlank;
	bigtime_t deadline = kms_monotonic_time() + timeout;
	status_t status = B_OK;
	while (true) {
		output = _FindOutput(outputID);
		if (output == NULL)
			break;
		if (output->lastBlank != last)
			break;
		bigtime_t now = kms_monotonic_time();
		if (now >= deadline) {
			status = B_TIMED_OUT;
			break;
		}
		struct timespec until;
		clock_gettime(CLOCK_REALTIME, &until);
		bigtime_t wait = min_c(deadline - now, (bigtime_t)100000);
		until.tv_nsec += wait * 1000;
		until.tv_sec += until.tv_nsec / 1000000000;
		until.tv_nsec %= 1000000000;
		pthread_cond_timedwait(&fCondition, &fLock, &until);
	}
	pthread_mutex_unlock(&fLock);
	return status;
}


status_t
KmsDevice::ScanLine(uint32 outputID, int32* _line, int32* _lines,
	bigtime_t* _scanStart, bigtime_t* _period)
{
	pthread_mutex_lock(&fLock);
	Output* output = _FindOutput(outputID);
	const KmsLayoutOutput* driven = NULL;
	for (uint32 i = 0; i < fCurrent.count; i++) {
		if (fCurrent.outputs[i].id == outputID && fCurrent.outputs[i].enabled)
			driven = &fCurrent.outputs[i];
	}
	if (output == NULL || driven == NULL || output->crtcIndex < 0
		|| !fSession.IsActive() || fDPMSMode != B_DPMS_ON) {
		pthread_mutex_unlock(&fLock);
		return B_UNSUPPORTED;
	}
	drmVBlank blank;
	memset(&blank, 0, sizeof(blank));
	blank.request.type = (drmVBlankSeqType)(DRM_VBLANK_RELATIVE
		| vblank_crtc_bits(output->crtcIndex));
	blank.request.sequence = 0;
	status_t status = B_OK;
	if (drmWaitVBlank(Fd(), &blank) != 0)
		status = B_UNSUPPORTED;
	else {
		bigtime_t last = (bigtime_t)blank.reply.tval_sec * 1000000
			+ blank.reply.tval_usec;
		kms_scan_position(driven->timing, driven->height,
			kms_monotonic_time(), last, *_line, *_lines, *_scanStart,
			*_period);
		if (*_scanStart == 0)
			status = B_UNSUPPORTED;
	}
	pthread_mutex_unlock(&fLock);
	return status;
}


status_t
KmsDevice::WaitIdle(bigtime_t timeout)
{
	bigtime_t deadline = kms_monotonic_time() + timeout;
	pthread_mutex_lock(&fLock);
	status_t status = B_OK;
	while (true) {
		bool busy = fRendering;
		for (size_t s = 0; s < fScanouts.size(); s++) {
			Scanout* scanout = fScanouts[s];
			busy |= !scanout->damage.empty() || scanout->full
				|| scanout->queued != NULL || scanout->pending != NULL
				|| scanout->rendering;
		}
		if (!busy)
			break;
		bigtime_t now = kms_monotonic_time();
		if (now >= deadline) {
			status = B_TIMED_OUT;
			break;
		}
		struct timespec until;
		clock_gettime(CLOCK_REALTIME, &until);
		bigtime_t wait = min_c(deadline - now, (bigtime_t)20000);
		until.tv_nsec += wait * 1000;
		until.tv_sec += until.tv_nsec / 1000000000;
		until.tv_nsec %= 1000000000;
		pthread_cond_timedwait(&fCondition, &fLock, &until);
	}
	pthread_mutex_unlock(&fLock);
	return status;
}


status_t
KmsDevice::ReadBack(uint32 outputID, std::vector<uint32>& pixels,
	uint32* _width, uint32* _height, KmsRect* _destination)
{
	pthread_mutex_lock(&fLock);
	Scanout* scanout = NULL;
	for (size_t s = 0; s < fScanouts.size() && scanout == NULL; s++) {
		for (size_t o = 0; o < fScanouts[s]->outputs.size(); o++) {
			if (fScanouts[s]->outputs[o] == outputID)
				scanout = fScanouts[s];
		}
	}
	status_t status = B_ENTRY_NOT_FOUND;
	if (scanout != NULL && scanout->shown != NULL) {
		KmsBuffer* buffer = scanout->shown;
		uint8* bits = buffer->Map(true);
		if (bits == NULL)
			status = B_NOT_SUPPORTED;
		else {
			pixels.resize((size_t)buffer->Width() * buffer->Height());
			for (uint32 y = 0; y < buffer->Height(); y++) {
				memcpy(&pixels[(size_t)y * buffer->Width()],
					bits + (size_t)y * buffer->Stride(), buffer->Width() * 4);
			}
			buffer->Unmap();
			*_width = buffer->Width();
			*_height = buffer->Height();
			if (_destination != NULL)
				*_destination = scanout->destination;
			status = B_OK;
		}
	}
	pthread_mutex_unlock(&fLock);
	return status;
}


// #pragma mark - power


status_t
KmsDevice::SetDPMSMode(uint32 mode)
{
	uint64 value;
	switch (mode) {
		case B_DPMS_ON:
			value = DRM_MODE_DPMS_ON;
			break;
		case B_DPMS_STAND_BY:
			value = DRM_MODE_DPMS_STANDBY;
			break;
		case B_DPMS_SUSPEND:
			value = DRM_MODE_DPMS_SUSPEND;
			break;
		case B_DPMS_OFF:
			value = DRM_MODE_DPMS_OFF;
			break;
		default:
			return B_BAD_VALUE;
	}
	pthread_mutex_lock(&fLock);
	if (mode == fDPMSMode) {
		pthread_mutex_unlock(&fLock);
		return B_OK;
	}
	if (mode != B_DPMS_ON) {
		// no flip may be under way on a CRTC that goes off
		_Drain(200000);
		_HideCursors();
	}
	status_t status = B_OK;
	drmModeAtomicReq* request = NULL;
	for (size_t o = 0; o < fOutputs.size() && status == B_OK; o++) {
		Output& output = fOutputs[o];
		if (output.crtcIndex < 0)
			continue;
		if (output.dpmsProperty != 0) {
			// the connector's DPMS property (atomic drivers map it to the
			// CRTC's ACTIVE)
			if (drmModeConnectorSetProperty(Fd(), output.connector,
					output.dpmsProperty, value) != 0)
				status = errno_status();
		} else if (fAtomic) {
			if (request == NULL)
				request = drmModeAtomicAlloc();
			if (request != NULL) {
				const Crtc& crtc = fCrtcs[output.crtcIndex];
				drmModeAtomicAddProperty(request, crtc.id, crtc.active,
					mode == B_DPMS_ON ? 1 : 0);
			}
		}
	}
	if (status == B_OK && request != NULL && drmModeAtomicCommit(Fd(),
			request, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL) != 0)
		status = errno_status();
	if (request != NULL)
		drmModeAtomicFree(request);
	TRACE("displays %s: %s\n", mode == B_DPMS_ON ? "on" : "off",
		status == B_OK ? "done" : strerror(-status));
	if (status == B_OK) {
		fDPMSMode = mode;
		if (mode == B_DPMS_ON) {
			for (size_t s = 0; s < fScanouts.size(); s++)
				fScanouts[s]->full = true;
			for (size_t o = 0; o < fOutputs.size(); o++) {
				if (fOutputs[o].crtcIndex >= 0)
					_ApplyCursor(fOutputs[o]);
			}
			pthread_cond_broadcast(&fCondition);
		}
	}
	pthread_mutex_unlock(&fLock);
	return status;
}


// #pragma mark - cursor


status_t
KmsDevice::SetCursor(const uint8* bits, uint32 bytesPerRow, uint32 width,
	uint32 height, int32 hotX, int32 hotY)
{
	pthread_mutex_lock(&fLock);
	fCursorImage.resize((size_t)width * height);
	for (uint32 y = 0; y < height; y++)
		memcpy(&fCursorImage[(size_t)y * width], bits + y * bytesPerRow,
			width * 4);
	fCursorImageWidth = width;
	fCursorImageHeight = height;
	fCursorHotX = hotX;
	fCursorHotY = hotY;
	fCursorOk = true;
	for (size_t o = 0; o < fOutputs.size(); o++) {
		fOutputs[o].cursorShown = false;
		if (fOutputs[o].crtcIndex >= 0)
			_ApplyCursor(fOutputs[o]);
	}
	status_t status = fCursorOk ? B_OK : B_NOT_SUPPORTED;
	if (!fCursorOk)
		_HideCursors();
	pthread_mutex_unlock(&fLock);
	return status;
}


/*!	Puts the cursor on the output's CRTC: its image scaled from the frame
	buffer's density to the monitor's, where the hot spot lands on the
	monitor pixel showing the pointer's buffer pixel. A cursor too large
	for the plane makes fCursorOk false: app_server draws it. Locked. */
void
KmsDevice::_ApplyCursor(Output& output)
{
	if (output.crtcIndex < 0)
		return;
	uint32 crtc = fCrtcs[output.crtcIndex].id;
	const KmsLayoutOutput* driven = NULL;
	for (uint32 i = 0; i < fCurrent.count; i++) {
		if (fCurrent.outputs[i].id == output.connector
			&& fCurrent.outputs[i].enabled)
			driven = &fCurrent.outputs[i];
	}
	if (driven == NULL || !fCursorVisible || !fCursorOk
		|| fCursorImage.empty() || !fSession.IsActive()
		|| fDPMSMode != B_DPMS_ON) {
		if (output.cursorShown)
			drmModeSetCursor(Fd(), crtc, 0, 0, 0);
		output.cursorShown = false;
		return;
	}

	int32 x, y, hotX, hotY;
	kms_cursor_position(*driven, fCursorX, fCursorY, fCursorHotX,
		fCursorHotY, x, y, hotX, hotY);
	if (!output.cursorShown) {
		KmsRect destination = kms_destination(*driven);
		uint32 width = fCursorImageWidth;
		uint32 height = fCursorImageHeight;
		if (driven->width != 0 && driven->height != 0) {
			width = (uint32)((uint64)fCursorImageWidth * destination.width
				/ driven->width);
			height = (uint32)((uint64)fCursorImageHeight * destination.height
				/ driven->height);
		}
		if (width == 0 || height == 0 || width > fCursorWidth
			|| height > fCursorHeight) {
			fCursorOk = false;
			return;
		}
		if (output.cursor == NULL) {
			output.cursor = KmsBuffer::CreateDumb(Fd(), fCursorWidth,
				fCursorHeight, DRM_FORMAT_ARGB8888);
		}
		if (output.cursor == NULL) {
			fCursorOk = false;
			return;
		}
		uint8* bits = output.cursor->Map();
		uint32 stride = output.cursor->Stride();
		for (uint32 row = 0; row < fCursorHeight; row++)
			memset(bits + (size_t)row * stride, 0, fCursorWidth * 4);
		KmsScaler scaler;
		if (scaler.SetTo(fCursorImageWidth, fCursorImageHeight, width,
				height) == B_OK) {
			scaler.Scale((const uint8*)&fCursorImage[0],
				fCursorImageWidth * 4, bits, stride,
				KmsRect(0, 0, width, height), false);
		}
		output.cursor->Unmap();
		if (drmModeSetCursor2(Fd(), crtc, output.cursor->Handle(),
				fCursorWidth, fCursorHeight, hotX, hotY) != 0
			&& drmModeSetCursor(Fd(), crtc, output.cursor->Handle(),
				fCursorWidth, fCursorHeight) != 0) {
			TRACE("no hardware cursor on %s: %s\n", output.name,
				strerror(errno));
			fCursorOk = false;
			return;
		}
		output.cursorShown = true;
	}
	drmModeMoveCursor(Fd(), crtc, x, y);
}


void
KmsDevice::_HideCursors()
{
	for (size_t o = 0; o < fOutputs.size(); o++) {
		Output& output = fOutputs[o];
		if (output.cursorShown && output.crtcIndex >= 0)
			drmModeSetCursor(Fd(), fCrtcs[output.crtcIndex].id, 0, 0, 0);
		output.cursorShown = false;
	}
}


void
KmsDevice::MoveCursor(int32 x, int32 y)
{
	pthread_mutex_lock(&fLock);
	fCursorX = x;
	fCursorY = y;
	for (size_t o = 0; o < fOutputs.size(); o++) {
		if (fOutputs[o].crtcIndex >= 0 && fOutputs[o].cursorShown)
			_ApplyCursor(fOutputs[o]);
	}
	pthread_mutex_unlock(&fLock);
}


void
KmsDevice::ShowCursor(bool visible)
{
	pthread_mutex_lock(&fLock);
	fCursorVisible = visible;
	for (size_t o = 0; o < fOutputs.size(); o++) {
		if (fOutputs[o].crtcIndex >= 0)
			_ApplyCursor(fOutputs[o]);
	}
	pthread_mutex_unlock(&fLock);
}


// #pragma mark - gamma


/*!	The white point of a black body at \a kelvin, as factors of the
	channels (Tanner Helland's fit of the CIE data, which redshift and
	VitruvianOS's night light use too); 6500 K is 1, 1, 1. */
static void
temperature_factors(float kelvin, float& red, float& green, float& blue)
{
	double t = kelvin / 100.0;
	double r, g, b;
	if (t <= 66) {
		r = 255;
		g = 99.4708025861 * log(t) - 161.1195681661;
		b = t <= 19 ? 0 : 138.5177312231 * log(t - 10) - 305.0447927307;
	} else {
		r = 329.698727446 * pow(t - 60, -0.1332047592);
		g = 288.1221695283 * pow(t - 60, -0.0755148492);
		b = 255;
	}
	red = (float)(r < 0 ? 0 : r > 255 ? 1 : r / 255);
	green = (float)(g < 0 ? 0 : g > 255 ? 1 : g / 255);
	blue = (float)(b < 0 ? 0 : b > 255 ? 1 : b / 255);
}


/*!	The CRTC's gamma ramp for the temperature (legacy gamma, which atomic
	drivers map to GAMMA_LUT). Locked. */
void
KmsDevice::_ApplyGamma(Output& output)
{
	if (output.crtcIndex < 0)
		return;
	uint32 crtcId = fCrtcs[output.crtcIndex].id;
	drmModeCrtc* crtc = drmModeGetCrtc(Fd(), crtcId);
	if (crtc == NULL)
		return;
	int size = crtc->gamma_size;
	drmModeFreeCrtc(crtc);
	if (size <= 1)
		return;
	if (fabsf(fTemperature - 6500.0f) < 1 && !output.gammaSet)
		return;
	float factor[3];
	temperature_factors(fTemperature, factor[0], factor[1], factor[2]);
	std::vector<uint16> ramps(size * 3);
	for (int i = 0; i < size; i++) {
		double value = (double)i / (size - 1) * 65535;
		for (int c = 0; c < 3; c++)
			ramps[c * size + i] = (uint16)(value * factor[c] + 0.5);
	}
	if (drmModeCrtcSetGamma(Fd(), crtcId, size, &ramps[0], &ramps[size],
			&ramps[2 * size]) == 0)
		output.gammaSet = true;
}


status_t
KmsDevice::SetTemperature(float kelvin)
{
	if (kelvin < 1000 || kelvin > 12000)
		return B_BAD_VALUE;
	pthread_mutex_lock(&fLock);
	fTemperature = kelvin;
	bool any = false;
	for (size_t o = 0; o < fOutputs.size(); o++) {
		_ApplyGamma(fOutputs[o]);
		any |= fOutputs[o].gammaSet;
	}
	pthread_mutex_unlock(&fLock);
	return any || fabsf(kelvin - 6500.0f) < 1 ? B_OK : B_NOT_SUPPORTED;
}


status_t
KmsDevice::GetOutputName(uint32 outputID, char* name, size_t size)
{
	pthread_mutex_lock(&fLock);
	const Output* output = NULL;
	if (outputID == 0) {
		const KmsLayoutOutput* primary = fModeSet
			? kms_layout_primary(fCurrent) : NULL;
		if (primary != NULL)
			output = _FindOutput(primary->id);
	} else
		output = _FindOutput(outputID);
	status_t status = B_ENTRY_NOT_FOUND;
	if (output != NULL) {
		strlcpy(name, output->name, size);
		status = B_OK;
	}
	pthread_mutex_unlock(&fLock);
	return status;
}


// #pragma mark - hot plug and the seat


/*!	Probes the connectors; true (and the listener told) when monitors
	came, went or were exchanged. What is driven stays until app_server
	sets a new layout. */
bool
KmsDevice::CheckOutputs()
{
	pthread_mutex_lock(&fLock);
	std::vector<Output> before = fOutputs;
	for (size_t i = 0; i < before.size(); i++)
		before[i].cursor = NULL;
	_ProbeOutputs(true);
	bool changed = _OutputsDiffer(before);
	if (changed) {
		// a layout to set for monitors that are no more is to forget
		fHasPending = false;
	}
	KmsDeviceListener* listener = fListener;
	pthread_mutex_unlock(&fLock);
	if (changed) {
		TRACE("monitors changed\n");
		if (listener != NULL)
			listener->KmsOutputsChanged();
	}
	return changed;
}


void
KmsDevice::_HandleUdev()
{
#ifdef VOS_HAVE_KMS_UDEV
	struct udev_device* device = udev_monitor_receive_device(fUdevMonitor);
	if (device == NULL)
		return;
	const char* hotplug = udev_device_get_property_value(device, "HOTPLUG");
	bool ours = udev_device_get_devnum(device) == fDevice;
	udev_device_unref(device);
	if (ours && hotplug != NULL && strcmp(hotplug, "1") == 0)
		CheckOutputs();
#endif
}


void
KmsDevice::SetSessionActive(bool active)
{
	if (active == fSession.IsActive())
		return;
	fSession.SetActive(active);
	if (active)
		SessionEnabled();
	else
		SessionDisabled();
}


void
KmsDevice::SessionEnabled()
{
	// on the event thread (libseat's dispatch)
	pthread_mutex_lock(&fLock);
	TRACE("session enabled\n");
	fFlipBroken = false;
	fFlipFailures = 0;
	if (fModeSet) {
		KmsLayout* layout = (KmsLayout*)malloc(sizeof(KmsLayout));
		if (layout != NULL) {
			*layout = fCurrent;
			if (_ApplyLayout(*layout) != B_OK)
				TRACE("could not show the screen again\n");
			free(layout);
		}
	}
	KmsDeviceListener* listener = fListener;
	pthread_mutex_unlock(&fLock);
	// monitors may have changed while away
	CheckOutputs();
	if (listener != NULL)
		listener->KmsSessionChanged(true);
}


void
KmsDevice::SessionDisabled()
{
	pthread_mutex_lock(&fLock);
	TRACE("session disabled\n");
	_Drain(200000);
	_HideCursors();
	KmsDeviceListener* listener = fListener;
	pthread_mutex_unlock(&fLock);
	fSession.AcknowledgeDisable();
	if (listener != NULL)
		listener->KmsSessionChanged(false);
}


// #pragma mark - threads


void
KmsDevice::_Wake()
{
	if (fWakeFd >= 0) {
		uint64_t one = 1;
		ssize_t written = write(fWakeFd, &one, sizeof(one));
		(void)written;
	}
}


/*static*/ void*
KmsDevice::_EventThreadEntry(void* data)
{
	((KmsDevice*)data)->_EventThread();
	return NULL;
}


void
KmsDevice::_EventThread()
{
	sEventDevice = this;
	while (!fQuit) {
		pthread_mutex_lock(&fLock);
		_ArmBlanks();
		bool waiting = kms_monotonic_time() < fRetraceWantedUntil;
		bool retry = false;
		for (size_t s = 0; s < fScanouts.size(); s++) {
			Scanout* scanout = fScanouts[s];
			if (scanout->queued != NULL && scanout->pending == NULL) {
				// a commit that found the device busy
				_Commit(scanout);
				retry |= scanout->queued != NULL;
			}
		}
		pthread_mutex_unlock(&fLock);

		struct pollfd fds[4];
		int count = 0;
		fds[count++] = (struct pollfd){ Fd(), POLLIN, 0 };
		int wakeIndex = -1, udevIndex = -1, seatIndex = -1;
		if (fWakeFd >= 0) {
			wakeIndex = count;
			fds[count++] = (struct pollfd){ fWakeFd, POLLIN, 0 };
		}
#ifdef VOS_HAVE_KMS_UDEV
		if (fUdevMonitor != NULL) {
			udevIndex = count;
			fds[count++] = (struct pollfd){
				udev_monitor_get_fd(fUdevMonitor), POLLIN, 0 };
		}
#endif
		if (fSession.DispatchFd() >= 0) {
			seatIndex = count;
			fds[count++] = (struct pollfd){ fSession.DispatchFd(), POLLIN, 0 };
		}
		int timeout = retry ? 4 : waiting ? 100 : 1000;
		int result = poll(fds, count, timeout);
		if (result < 0 && errno != EINTR)
			break;
		if (result <= 0)
			continue;
		if ((fds[0].revents & POLLIN) != 0) {
			drmEventContext context;
			memset(&context, 0, sizeof(context));
			context.version = 3;
			context.vblank_handler = _BlankHandler;
			context.page_flip_handler2 = _FlipHandler;
			drmHandleEvent(Fd(), &context);
		}
		if (wakeIndex >= 0 && (fds[wakeIndex].revents & POLLIN) != 0) {
			uint64_t value;
			ssize_t bytes = read(fWakeFd, &value, sizeof(value));
			(void)bytes;
		}
		if (udevIndex >= 0 && (fds[udevIndex].revents & POLLIN) != 0)
			_HandleUdev();
		if (seatIndex >= 0 && (fds[seatIndex].revents & (POLLIN | POLLHUP))
				!= 0)
			fSession.Dispatch();
	}
	sEventDevice = NULL;
}


/*static*/ void*
KmsDevice::_PresentThreadEntry(void* data)
{
	((KmsDevice*)data)->_PresentThread();
	return NULL;
}


/*!	Copies what was drawn into the scan-out buffers and flips them: the
	frame buffer is read with app_server's read lock, never with the
	device locked (the lock order is app_server's lock, then the device's).
*/
void
KmsDevice::_PresentThread()
{
	struct Job {
		Scanout*				scanout;
		std::vector<KmsRect>	damage;
		bool					full;
		int32					slot;
	};

	pthread_mutex_lock(&fLock);
	while (!fQuit) {
#ifdef VOS_HAVE_KMS_EGL
		// buffers the surfaces get back
		for (size_t s = 0; s < fScanouts.size(); s++) {
			Scanout* scanout = fScanouts[s];
			if (scanout->gl == NULL || scanout->rendering)
				continue;
			for (size_t i = 0; i < scanout->release.size(); i++) {
				KmsBuffer* buffer = (KmsBuffer*)scanout->release[i];
				if (buffer == scanout->shown || buffer == scanout->pending
					|| buffer == scanout->queued)
					continue;
				for (size_t b = 0; b < scanout->glBuffers.size(); b++) {
					if (scanout->glBuffers[b] == buffer) {
						fGles->ReleaseFrame(scanout->gl, buffer->Bo());
						scanout->glBuffers.erase(scanout->glBuffers.begin()
							+ b);
						delete buffer;
						break;
					}
				}
			}
			scanout->release.clear();
		}
#endif
		bool work = false;
		for (size_t s = 0; s < fScanouts.size() && !work; s++)
			work = _CanRender(fScanouts[s]);
		if (!work || fFrontSource == NULL) {
			pthread_cond_wait(&fCondition, &fLock);
			continue;
		}
		KmsFrontSource* source = fFrontSource;
		pthread_mutex_unlock(&fLock);

		KmsFrontView front;
		if (!source->LockFront(front)) {
			usleep(5000);
			pthread_mutex_lock(&fLock);
			continue;
		}

		pthread_mutex_lock(&fLock);
		std::vector<Job> jobs;
		for (size_t s = 0; s < fScanouts.size(); s++) {
			Scanout* scanout = fScanouts[s];
			if (!_CanRender(scanout))
				continue;
			Job job;
			job.scanout = scanout;
			job.damage.swap(scanout->damage);
			job.full = scanout->full;
			job.slot = scanout->gl != NULL ? 0 : _FreeSlot(scanout);
			scanout->full = false;
			scanout->rendering = true;
			jobs.push_back(job);
		}
		fRendering = !jobs.empty();
		uint32 generation = fGeneration;
		pthread_mutex_unlock(&fLock);

		for (size_t j = 0; j < jobs.size(); j++) {
			_Render(jobs[j].scanout, front, jobs[j].damage, jobs[j].slot,
				jobs[j].full);
		}
		source->UnlockFront();

		pthread_mutex_lock(&fLock);
		fRendering = false;
		for (size_t j = 0; j < jobs.size() && generation == fGeneration; j++) {
			Scanout* scanout = jobs[j].scanout;
			scanout->rendering = false;
#ifdef VOS_HAVE_KMS_EGL
			if (scanout->gl != NULL) {
				// _Render() left the new frame last in the release list
				if (scanout->release.empty() || jobs[j].damage.empty()) {
					scanout->full = true;
					continue;
				}
				KmsBuffer* frame = (KmsBuffer*)scanout->release.back();
				scanout->release.pop_back();
				if (scanout->queued != NULL)
					scanout->release.push_back((gbm_bo*)scanout->queued);
				scanout->queued = frame;
				_Commit(scanout);
				continue;
			}
#endif
			int32 slot = jobs[j].slot;
			// what changed is missing from the other buffers
			for (size_t b = 0; b < scanout->buffers.size(); b++) {
				if ((int32)b == slot)
					continue;
				std::vector<KmsRect>& stale = scanout->stale[b];
				for (size_t i = 0; i < jobs[j].damage.size(); i++)
					stale.push_back(jobs[j].damage[i]);
				if (stale.size() > kMaxDamageRects) {
					KmsRect all;
					for (size_t i = 0; i < stale.size(); i++)
						all = all.Union(stale[i]);
					stale.clear();
					stale.push_back(all);
				}
			}
			scanout->stale[slot].clear();
			scanout->queued = scanout->buffers[slot];
			_Commit(scanout);
		}
		for (size_t j = 0; j < jobs.size(); j++)
			jobs[j].scanout->rendering = false;
		pthread_cond_broadcast(&fCondition);
	}
	pthread_mutex_unlock(&fLock);
}
