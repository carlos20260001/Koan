/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef KMS_GLES_H
#define KMS_GLES_H


/*	The GPU pass of the KMS backend (VOS_HAVE_KMS_EGL): EGL on the device's
	GBM, one GLES2 context. Each scan-out is a gbm_surface of its buffer's
	size; a frame uploads the damaged parts of app_server's frame buffer
	(what Painter drew, B_RGB32) into a texture of the region's size and
	draws it into the surface, filtered to the monitor's size there: the
	fractional render scale's resample costs the CPU nothing. Only the part
	of the surface the buffer's age says is out of date is drawn.

	All calls come from one thread (the device's present thread).
*/


#include "KmsLayout.h"

#include <vector>


struct gbm_bo;
struct gbm_device;
struct KmsGlTarget;


class KmsGles {
public:
								KmsGles();
								~KmsGles();

			status_t			Init(gbm_device* device);
			void				Uninit();
			bool				IsInitialized() const { return fDisplay != NULL; }
			// llvmpipe/softpipe: the CPU path is faster than that
			bool				IsSoftware() const { return fSoftware; }
			const char*			Renderer() const { return fRenderer; }
			int32				MaxTextureSize() const
									{ return fMaxTextureSize; }

			// a surface of width x height showing a region of
			// regionWidth x regionHeight at destination
			KmsGlTarget*		CreateTarget(uint32 width, uint32 height,
									uint32 regionWidth, uint32 regionHeight,
									const KmsRect& destination,
									status_t* error = NULL);
			void				DestroyTarget(KmsGlTarget* target);

			/*!	Draws a frame: \a damage are the parts of the region (region
				coordinates) that changed since the last frame, \a source
				the region's first pixel in the frame buffer. Returns the
				frame's buffer object, locked until ReleaseFrame(). */
			gbm_bo*				Render(KmsGlTarget* target,
									const uint8* source,
									uint32 sourceBytesPerRow,
									const std::vector<KmsRect>& damage,
									bool full);
			void				ReleaseFrame(KmsGlTarget* target, gbm_bo* bo);

private:
			bool				_MakeCurrent(KmsGlTarget* target);
			void				_Upload(KmsGlTarget* target,
									const uint8* source,
									uint32 sourceBytesPerRow,
									const KmsRect& rect);

			void*				fDisplay;		// EGLDisplay
			void*				fConfig;		// EGLConfig
			void*				fContext;		// EGLContext
			gbm_device*			fDevice;
			uint32				fProgram;
			int32				fPositionLocation;
			int32				fTextureLocation;
			bool				fUnpackSubimage;
			bool				fBufferAge;
			bool				fSoftware;
			int32				fMaxTextureSize;
			char				fRenderer[128];
			std::vector<uint8>	fStaging;
};


#endif	// KMS_GLES_H
