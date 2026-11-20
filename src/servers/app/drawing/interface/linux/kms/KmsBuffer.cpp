/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KmsBuffer.h"

#include <new>

#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#ifdef VOS_HAVE_KMS_GBM
#	include <gbm.h>
#endif


static status_t
errno_status()
{
	return errno > 0 ? -errno : B_ERROR;
}


KmsBuffer::KmsBuffer(int fd, Kind kind)
	:
	fFd(fd),
	fKind(kind),
	fHandle(0),
	fFramebuffer(0),
	fWidth(0),
	fHeight(0),
	fFormat(DRM_FORMAT_XRGB8888),
	fStride(0),
	fSize(0),
	fModifier(DRM_FORMAT_MOD_INVALID),
	fMap(NULL),
	fBo(NULL),
	fMapData(NULL),
	fMapCount(0)
{
}


KmsBuffer::~KmsBuffer()
{
	if (fFramebuffer != 0)
		drmModeRmFB(fFd, fFramebuffer);
	switch (fKind) {
		case KIND_DUMB:
			if (fMap != NULL)
				munmap(fMap, fSize);
			if (fHandle != 0) {
				struct drm_mode_destroy_dumb destroy;
				memset(&destroy, 0, sizeof(destroy));
				destroy.handle = fHandle;
				drmIoctl(fFd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
			}
			break;
		case KIND_GBM:
#ifdef VOS_HAVE_KMS_GBM
			while (fMapCount > 0)
				Unmap();
			if (fBo != NULL)
				gbm_bo_destroy(fBo);
#endif
			break;
		case KIND_GBM_SURFACE:
#ifdef VOS_HAVE_KMS_GBM
			while (fMapCount > 0)
				Unmap();
#endif
			// the surface owns the buffer object
			break;
	}
}


status_t
KmsBuffer::_AddFramebuffer()
{
	uint32 handles[4] = { fHandle, 0, 0, 0 };
	uint32 pitches[4] = { fStride, 0, 0, 0 };
	uint32 offsets[4] = { 0, 0, 0, 0 };
	int result;
	if (fModifier != DRM_FORMAT_MOD_INVALID
		&& fModifier != DRM_FORMAT_MOD_LINEAR) {
		uint64_t modifiers[4] = { fModifier, 0, 0, 0 };
		result = drmModeAddFB2WithModifiers(fFd, fWidth, fHeight, fFormat,
			handles, pitches, offsets, modifiers, &fFramebuffer,
			DRM_MODE_FB_MODIFIERS);
	} else {
		result = drmModeAddFB2(fFd, fWidth, fHeight, fFormat, handles,
			pitches, offsets, &fFramebuffer, 0);
	}
	if (result != 0 && fFormat == DRM_FORMAT_XRGB8888) {
		// drivers older than ADDFB2
		result = drmModeAddFB(fFd, fWidth, fHeight, 24, 32, fStride, fHandle,
			&fFramebuffer);
	}
	return result == 0 ? B_OK : errno_status();
}


/*static*/ KmsBuffer*
KmsBuffer::CreateDumb(int fd, uint32 width, uint32 height, uint32 format,
	status_t* _error)
{
	status_t error = B_NO_MEMORY;
	KmsBuffer* buffer = new(std::nothrow) KmsBuffer(fd, KIND_DUMB);
	if (buffer != NULL) {
		struct drm_mode_create_dumb create;
		memset(&create, 0, sizeof(create));
		create.width = width;
		create.height = height;
		create.bpp = 32;
		error = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) == 0
			? B_OK : errno_status();
		if (error == B_OK) {
			buffer->fHandle = create.handle;
			buffer->fStride = create.pitch;
			buffer->fSize = create.size;
			buffer->fWidth = width;
			buffer->fHeight = height;
			buffer->fFormat = format;
			error = buffer->_AddFramebuffer();
		}
		if (error == B_OK) {
			struct drm_mode_map_dumb map;
			memset(&map, 0, sizeof(map));
			map.handle = buffer->fHandle;
			error = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) == 0
				? B_OK : errno_status();
			if (error == B_OK) {
				void* address = mmap(NULL, buffer->fSize,
					PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
				if (address == MAP_FAILED)
					error = errno_status();
				else
					buffer->fMap = (uint8*)address;
			}
		}
		if (error == B_OK)
			memset(buffer->fMap, 0, buffer->fSize);
		else {
			delete buffer;
			buffer = NULL;
		}
	}
	if (_error != NULL)
		*_error = error;
	return buffer;
}


/*static*/ KmsBuffer*
KmsBuffer::CreateGbm(int fd, gbm_device* device, uint32 width, uint32 height,
	uint32 format, uint32 usage, status_t* _error)
{
#ifdef VOS_HAVE_KMS_GBM
	status_t error = B_NO_MEMORY;
	KmsBuffer* buffer = new(std::nothrow) KmsBuffer(fd, KIND_GBM);
	if (buffer != NULL) {
		buffer->fBo = gbm_bo_create(device, width, height, format, usage);
		error = buffer->fBo != NULL ? B_OK : errno_status();
		if (error == B_OK) {
			buffer->fHandle = gbm_bo_get_handle(buffer->fBo).u32;
			buffer->fStride = gbm_bo_get_stride(buffer->fBo);
			buffer->fModifier = gbm_bo_get_modifier(buffer->fBo);
			buffer->fWidth = width;
			buffer->fHeight = height;
			buffer->fFormat = format;
			error = buffer->_AddFramebuffer();
		}
		if (error == B_OK) {
			// defined contents: the buffer is scanned out at once
			uint8* bits = buffer->Map();
			if (bits == NULL)
				error = B_ERROR;
			else {
				for (uint32 y = 0; y < height; y++)
					memset(bits + (size_t)y * buffer->fStride, 0, width * 4);
				buffer->Unmap();
			}
		}
		if (error != B_OK) {
			delete buffer;
			buffer = NULL;
		}
	}
	if (_error != NULL)
		*_error = error;
	return buffer;
#else
	if (_error != NULL)
		*_error = B_NOT_SUPPORTED;
	return NULL;
#endif
}


/*static*/ KmsBuffer*
KmsBuffer::ForSurfaceBo(int fd, gbm_bo* bo, status_t* _error)
{
#ifdef VOS_HAVE_KMS_GBM
	status_t error = B_NO_MEMORY;
	KmsBuffer* buffer = new(std::nothrow) KmsBuffer(fd, KIND_GBM_SURFACE);
	if (buffer != NULL) {
		buffer->fBo = bo;
		buffer->fHandle = gbm_bo_get_handle(bo).u32;
		buffer->fStride = gbm_bo_get_stride(bo);
		buffer->fModifier = gbm_bo_get_modifier(bo);
		buffer->fWidth = gbm_bo_get_width(bo);
		buffer->fHeight = gbm_bo_get_height(bo);
		buffer->fFormat = gbm_bo_get_format(bo);
		error = buffer->_AddFramebuffer();
		if (error != B_OK) {
			delete buffer;
			buffer = NULL;
		}
	}
	if (_error != NULL)
		*_error = error;
	return buffer;
#else
	if (_error != NULL)
		*_error = B_NOT_SUPPORTED;
	return NULL;
#endif
}


uint8*
KmsBuffer::Map(bool read)
{
	if (fKind == KIND_DUMB)
		return fMap;
#ifdef VOS_HAVE_KMS_GBM
	if (fMapCount++ > 0)
		return fMap;
	uint32 stride = 0;
	void* data = NULL;
	void* address = gbm_bo_map(fBo, 0, 0, fWidth, fHeight,
		read ? GBM_BO_TRANSFER_READ_WRITE : GBM_BO_TRANSFER_WRITE, &stride,
		&data);
	if (address == NULL) {
		fMapCount--;
		return NULL;
	}
	fMap = (uint8*)address;
	fMapData = data;
	fStride = stride;
	return fMap;
#else
	return NULL;
#endif
}


void
KmsBuffer::Unmap()
{
	if (fKind == KIND_DUMB)
		return;
#ifdef VOS_HAVE_KMS_GBM
	if (fMapCount == 0 || --fMapCount > 0)
		return;
	gbm_bo_unmap(fBo, fMapData);
	fMap = NULL;
	fMapData = NULL;
	fStride = gbm_bo_get_stride(fBo);
#endif
}
