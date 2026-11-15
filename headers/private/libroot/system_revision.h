/*
 * Copyright 2011, Oliver Tappe <zooey@hirschkaefer.de>.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LIBROOT_SYSTEM_REVISION_H
#define _LIBROOT_SYSTEM_REVISION_H


/** The length of the system revision character array symbol living in libroot
    and the kernel */
#define SYSTEM_REVISION_LENGTH 128


#ifdef __cplusplus
extern "C" {
#endif


#ifdef __VOS__
/** returns the VOS (Vitruvian OS) revision tag (e.g. "rev-42").
    Falls back to the Linux kernel release string if no tag was baked in. */
#ifndef _KERNEL_MODE
const char* __get_vos_revision(void);
#endif
#else	/* !__VOS__ */
/** returns the system revision */
#ifdef _KERNEL_MODE
const char* get_haiku_revision(void);
#else
const char* __get_haiku_revision(void);
#endif
#endif	/* __VOS__ */


#ifdef __cplusplus
}
#endif


#endif	/* _LIBROOT_SYSTEM_REVISION_H */
