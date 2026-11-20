/*
 * Copyright 2026, VitruvianOS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef KMS_EDID_H
#define KMS_EDID_H


#include <Accelerant.h>
#include <SupportDefs.h>


// A connector's EDID blob (decode_edid's edid_decode()) as air/OS's monitor
// information, as AccelerantHWInterface::GetMonitorInfo() fills it in.
status_t	kms_edid_monitor_info(const uint8* edid, size_t length,
				monitor_info* info);

// the preferred (first detailed) timing's size, 0 when there is none
status_t	kms_edid_preferred_size(const uint8* edid, size_t length,
				uint32* width, uint32* height);


#endif	// KMS_EDID_H
