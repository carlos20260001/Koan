/*
 * Koan compatibility header: drm_fourcc.h
 *
 * Koan does not ship Linux kernel DRM headers. This header provides the
 * DRM fourcc pixel format codes used by wlroots so that the wlroots source
 * tree can compile without libdrm. All codes have unique values to avoid
 * duplicate case errors in switch statements.
 *
 * This is a compatibility layer for the Koan port, not a replacement for
 * libdrm on systems where it is available.
 */

#ifndef DRM_FOURCC_H
#define DRM_FOURCC_H

#include <stdint.h>

/* Fourcc codes are little-endian four-character codes. */
#define DRM_FOURCC(c0, c1, c2, c3) \
	((uint32_t)(c0) | ((uint32_t)(c1) << 8) | \
	 ((uint32_t)(c2) << 16) | ((uint32_t)(c3) << 24))

/* Invalid format. */
#define DRM_FORMAT_INVALID		0

/* Packed RGB formats (8-bit per channel). */
#define DRM_FORMAT_C8			DRM_FOURCC('C', '8', ' ', ' ')
#define DRM_FORMAT_RGB332		DRM_FOURCC('R', 'G', 'B', '2')
#define DRM_FORMAT_BGR233		DRM_FOURCC('B', 'G', 'R', '2')
#define DRM_FORMAT_XRGB4444		DRM_FOURCC('X', 'R', '1', '2')
#define DRM_FORMAT_XBGR4444		DRM_FOURCC('X', 'B', '1', '2')
#define DRM_FORMAT_XRGB1555		DRM_FOURCC('X', 'R', '1', '5')
#define DRM_FORMAT_XBGR1555		DRM_FOURCC('X', 'B', '1', '5')
#define DRM_FORMAT_ARGB4444		DRM_FOURCC('A', 'R', '1', '2')
#define DRM_FORMAT_ABGR4444		DRM_FOURCC('A', 'B', '1', '2')
#define DRM_FORMAT_ARGB1555		DRM_FOURCC('A', 'R', '1', '5')
#define DRM_FORMAT_ABGR1555		DRM_FOURCC('A', 'B', '1', '5')
#define DRM_FORMAT_RGB565		DRM_FOURCC('R', 'G', '1', '6')
#define DRM_FORMAT_BGR565		DRM_FOURCC('B', 'G', '1', '6')
#define DRM_FORMAT_RGB888		DRM_FOURCC('R', 'G', '2', '4')
#define DRM_FORMAT_BGR888		DRM_FOURCC('B', 'G', '2', '4')
#define DRM_FORMAT_XRGB8888		DRM_FOURCC('X', 'R', '2', '4')
#define DRM_FORMAT_XBGR8888		DRM_FOURCC('X', 'B', '2', '4')
#define DRM_FORMAT_ARGB8888		DRM_FOURCC('A', 'R', '2', '4')
#define DRM_FORMAT_ABGR8888		DRM_FOURCC('A', 'B', '2', '4')

/* Packed RGB formats (16-bit per channel). */
#define DRM_FORMAT_XRGB16161616	DRM_FOURCC('X', 'R', '4', '8')
#define DRM_FORMAT_XBGR16161616	DRM_FOURCC('X', 'B', '4', '8')
#define DRM_FORMAT_ARGB16161616	DRM_FOURCC('A', 'R', '4', '8')
#define DRM_FORMAT_ABGR16161616	DRM_FOURCC('A', 'B', '4', '8')

/* Float formats (16-bit per channel). */
#define DRM_FORMAT_XBGR16161616F	DRM_FOURCC('X', 'F', '3', '2')
#define DRM_FORMAT_ABGR16161616F	DRM_FOURCC('A', 'F', '3', '2')

/* Float formats (32-bit per channel). */
#define DRM_FORMAT_ABGR32323232F	DRM_FOURCC('A', 'F', '3', '2')
#define DRM_FORMAT_BGR323232F		DRM_FOURCC('B', 'F', '3', '2')
#define DRM_FORMAT_R16F			DRM_FOURCC('R', '1', '6', 'f')
#define DRM_FORMAT_R32F			DRM_FOURCC('R', '3', '2', 'f')
#define DRM_FORMAT_GR1616F		DRM_FOURCC('G', 'R', '1', '6')
#define DRM_FORMAT_GR3232F		DRM_FOURCC('G', 'R', '3', '2')

/* 8-bit formats. */
#define DRM_FORMAT_R8			DRM_FOURCC('R', '8', ' ', ' ')
#define DRM_FORMAT_GR88			DRM_FOURCC('G', 'R', '8', '8')

/* BGRA / RGBA packed formats. */
#define DRM_FORMAT_BGRA8888		DRM_FOURCC('B', 'A', '2', '4')
#define DRM_FORMAT_BGRA5551		DRM_FOURCC('B', 'A', '1', '5')
#define DRM_FORMAT_BGRA4444		DRM_FOURCC('B', 'A', '1', '2')
#define DRM_FORMAT_RGBA8888		DRM_FOURCC('R', 'A', '2', '4')
#define DRM_FORMAT_RGBA5551		DRM_FOURCC('R', 'A', '1', '5')
#define DRM_FORMAT_RGBA4444		DRM_FOURCC('R', 'A', '1', '2')

/* BGRX / RGBX packed formats. */
#define DRM_FORMAT_BGRX8888		DRM_FOURCC('B', 'X', '2', '4')
#define DRM_FORMAT_BGRX5551		DRM_FOURCC('B', 'X', '1', '5')
#define DRM_FORMAT_BGRX4444		DRM_FOURCC('B', 'X', '1', '2')
#define DRM_FORMAT_RGBX8888		DRM_FOURCC('R', 'X', '2', '4')
#define DRM_FORMAT_RGBX5551		DRM_FOURCC('R', 'X', '1', '5')
#define DRM_FORMAT_RGBX4444		DRM_FOURCC('R', 'X', '1', '2')

/* 10-bit packed formats. */
#define DRM_FORMAT_XRGB2101010	DRM_FOURCC('X', 'R', '3', '0')
#define DRM_FORMAT_XBGR2101010	DRM_FOURCC('X', 'B', '3', '0')
#define DRM_FORMAT_ARGB2101010	DRM_FOURCC('A', 'R', '3', '0')
#define DRM_FORMAT_ABGR2101010	DRM_FOURCC('A', 'B', '3', '0')

/* YUV formats (10-bit and others). */
#define DRM_FORMAT_Y210			DRM_FOURCC('Y', '2', '1', '0')
#define DRM_FORMAT_Y212			DRM_FOURCC('Y', '2', '1', '2')
#define DRM_FORMAT_Y216			DRM_FOURCC('Y', '2', '1', '6')
#define DRM_FORMAT_Y410			DRM_FOURCC('Y', '4', '1', '0')
#define DRM_FORMAT_Y412			DRM_FOURCC('Y', '4', '1', '2')
#define DRM_FORMAT_Y416			DRM_FOURCC('Y', '4', '1', '6')
#define DRM_FORMAT_YUV420_10BIT	DRM_FOURCC('Y', '4', '2', 'A')
#define DRM_FORMAT_YUV420_8BIT	DRM_FOURCC('Y', '4', '2', '1')
#define DRM_FORMAT_YVU410		DRM_FOURCC('Y', 'V', '4', '0')
#define DRM_FORMAT_YVU411		DRM_FOURCC('Y', 'V', '4', '1')
#define DRM_FORMAT_YVU420		DRM_FOURCC('Y', 'V', '4', '2')
#define DRM_FORMAT_YVU422		DRM_FOURCC('Y', 'V', '4', '3')
#define DRM_FORMAT_YVU444		DRM_FOURCC('Y', 'V', '4', '4')

/* Additional YUV/planar formats. */
#define DRM_FORMAT_X0L0			DRM_FOURCC('X', '0', 'L', '0')
#define DRM_FORMAT_X0L2			DRM_FOURCC('X', '0', 'L', '2')
#define DRM_FORMAT_XVYU12_16161616	DRM_FOURCC('X', 'V', '1', '2')
#define DRM_FORMAT_XVYU16161616		DRM_FOURCC('X', 'V', '1', '6')
#define DRM_FORMAT_XVYU2101010		DRM_FOURCC('X', 'V', '3', '0')
#define DRM_FORMAT_XYUV8888		DRM_FOURCC('X', 'Y', 'U', 'V')
#define DRM_FORMAT_Y0L0			DRM_FOURCC('Y', '0', 'L', '0')
#define DRM_FORMAT_Y0L2			DRM_FOURCC('Y', '0', 'L', '2')

/* Semi-planar formats. */
#define DRM_FORMAT_P010			DRM_FOURCC('P', '0', '1', '0')
#define DRM_FORMAT_P012			DRM_FOURCC('P', '0', '1', '2')
#define DRM_FORMAT_P016			DRM_FOURCC('P', '0', '1', '6')
#define DRM_FORMAT_P030			DRM_FOURCC('P', '0', '3', '0')
#define DRM_FORMAT_P210			DRM_FOURCC('P', '2', '1', '0')

/* YUV formats. */
#define DRM_FORMAT_YUYV			DRM_FOURCC('Y', 'U', 'Y', 'V')
#define DRM_FORMAT_YVYU			DRM_FOURCC('Y', 'V', 'Y', 'U')
#define DRM_FORMAT_UYVY			DRM_FOURCC('U', 'Y', 'V', 'Y')
#define DRM_FORMAT_VYUY			DRM_FOURCC('V', 'Y', 'U', 'Y')
#define DRM_FORMAT_AYUV			DRM_FOURCC('A', 'Y', 'U', 'V')
#define DRM_FORMAT_NV12			DRM_FOURCC('N', 'V', '1', '2')
#define DRM_FORMAT_NV21			DRM_FOURCC('N', 'V', '2', '1')
#define DRM_FORMAT_NV16			DRM_FOURCC('N', 'V', '1', '6')
#define DRM_FORMAT_NV61			DRM_FOURCC('N', 'V', '6', '1')
#define DRM_FORMAT_NV15			DRM_FOURCC('N', 'V', '1', '5')
#define DRM_FORMAT_NV20			DRM_FOURCC('N', 'V', '2', '0')
#define DRM_FORMAT_NV24			DRM_FOURCC('N', 'V', '2', '4')
#define DRM_FORMAT_NV30			DRM_FOURCC('N', 'V', '3', '0')
#define DRM_FORMAT_NV42			DRM_FOURCC('N', 'V', '4', '2')
#define DRM_FORMAT_YUV410		DRM_FOURCC('Y', '4', '1', 'B')
#define DRM_FORMAT_YUV411		DRM_FOURCC('Y', '4', '1', '1')
#define DRM_FORMAT_YUV420		DRM_FOURCC('Y', '4', '2', '0')
#define DRM_FORMAT_YUV422		DRM_FOURCC('Y', '4', '2', '2')
#define DRM_FORMAT_YUV444		DRM_FOURCC('Y', '4', '4', '4')

/* Q formats. */
#define DRM_FORMAT_Q401			DRM_FOURCC('Q', '4', '0', '1')
#define DRM_FORMAT_Q410			DRM_FOURCC('Q', '4', '1', '0')

/* VUY formats. */
#define DRM_FORMAT_VUY101010		DRM_FOURCC('V', 'U', '3', '0')
#define DRM_FORMAT_VUY888		DRM_FOURCC('V', 'U', '8', '8')

/* Signed planar formats. */
#define DRM_FORMAT_S010			DRM_FOURCC('S', '0', '1', '0')
#define DRM_FORMAT_S012			DRM_FOURCC('S', '0', '1', '2')
#define DRM_FORMAT_S016			DRM_FOURCC('S', '0', '1', '6')
#define DRM_FORMAT_S210			DRM_FOURCC('S', '2', '1', '0')
#define DRM_FORMAT_S212			DRM_FOURCC('S', '2', '1', '2')
#define DRM_FORMAT_S216			DRM_FOURCC('S', '2', '1', '6')
#define DRM_FORMAT_S410			DRM_FOURCC('S', '4', '1', '0')
#define DRM_FORMAT_S412			DRM_FOURCC('S', '4', '1', '2')
#define DRM_FORMAT_S416			DRM_FOURCC('S', '4', '1', '6')

/* BGR 16-bit. */
#define DRM_FORMAT_BGR161616		DRM_FOURCC('B', 'G', '1', '6')
#define DRM_FORMAT_BGR161616F		DRM_FOURCC('B', 'F', '1', '6')

/* Modifiers. */
#define DRM_FORMAT_MOD_INVALID	0
#define DRM_FORMAT_MOD_LINEAR	(1ULL << 63) /* DRM_FORMAT_MOD_LINEAR */

#endif /* DRM_FOURCC_H */
