/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef KMS_BUFFER_H
#define KMS_BUFFER_H


#include <SupportDefs.h>

#include <stdint.h>


struct gbm_bo;
struct gbm_device;


/*!	A scan-out buffer: a DRM frame buffer (XRGB8888 or ARGB8888) on a GBM
	buffer object or a dumb buffer. The CPU writes it between Map() and
	Unmap(); a dumb buffer stays mapped, a GBM buffer is mapped for each
	write (the driver may copy it to and from the GPU's memory).
*/
class KmsBuffer {
public:
	enum Kind {
		KIND_DUMB,
		KIND_GBM,
		KIND_GBM_SURFACE	// a buffer of a gbm_surface: not ours to free
	};

								~KmsBuffer();

	static	KmsBuffer*			CreateDumb(int fd, uint32 width, uint32 height,
									uint32 format, status_t* error = NULL);
	static	KmsBuffer*			CreateGbm(int fd, gbm_device* device,
									uint32 width, uint32 height,
									uint32 format, uint32 usage,
									status_t* error = NULL);
	static	KmsBuffer*			ForSurfaceBo(int fd, gbm_bo* bo,
									status_t* error = NULL);

			Kind				GetKind() const { return fKind; }
			uint32				FramebufferId() const { return fFramebuffer; }
			uint32				Handle() const { return fHandle; }
			uint32				Width() const { return fWidth; }
			uint32				Height() const { return fHeight; }
			uint32				Format() const { return fFormat; }
			gbm_bo*				Bo() const { return fBo; }

			// CPU access; Stride() is valid while mapped
			uint8*				Map(bool read = false);
			void				Unmap();
			uint32				Stride() const { return fStride; }

private:
								KmsBuffer(int fd, Kind kind);
			status_t			_AddFramebuffer();

			int					fFd;
			Kind				fKind;
			uint32				fHandle;
			uint32				fFramebuffer;
			uint32				fWidth;
			uint32				fHeight;
			uint32				fFormat;
			uint32				fStride;
			uint64				fSize;
			uint64_t			fModifier;
			uint8*				fMap;
			gbm_bo*				fBo;
			void*				fMapData;
			int32				fMapCount;
};


#endif	// KMS_BUFFER_H
