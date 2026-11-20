/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KmsEdid.h"

#include <stdio.h>
#include <string.h>

#include <edid.h>


status_t
kms_edid_monitor_info(const uint8* data, size_t length, monitor_info* info)
{
	if (data == NULL || info == NULL)
		return B_BAD_VALUE;
	if (length < sizeof(edid1_raw))
		return B_NOT_SUPPORTED;
	static const uint8 kHeader[8]
		= { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
	if (memcmp(data, kHeader, sizeof(kHeader)) != 0)
		return B_BAD_DATA;

	edid1_info edid;
	memset(&edid, 0, sizeof(edid));
	edid_decode(&edid, (const edid1_raw*)data);

	memset(info, 0, sizeof(monitor_info));
	strlcpy(info->vendor, edid.vendor.manufacturer, sizeof(info->vendor));
	if (edid.vendor.serial != 0) {
		snprintf(info->serial_number, sizeof(info->serial_number), "%" B_PRIu32,
			edid.vendor.serial);
	}
	info->product_id = edid.vendor.prod_id;
	info->produced.week = edid.vendor.week;
	info->produced.year = edid.vendor.year;
	info->width = edid.display.h_size;
	info->height = edid.display.v_size;

	for (uint32 i = 0; i < EDID1_NUM_DETAILED_MONITOR_DESC; ++i) {
		edid1_detailed_monitor* monitor = &edid.detailed_monitor[i];
		switch (monitor->monitor_desc_type) {
			case EDID1_SERIAL_NUMBER:
				strlcpy(info->serial_number, monitor->data.serial_number,
					sizeof(info->serial_number));
				break;
			case EDID1_MONITOR_NAME:
				strlcpy(info->name, monitor->data.monitor_name,
					sizeof(info->name));
				break;
			case EDID1_MONITOR_RANGES:
			{
				edid1_monitor_range& range = monitor->data.monitor_range;
				info->min_horizontal_frequency = range.min_h;
				info->max_horizontal_frequency = range.max_h;
				info->min_vertical_frequency = range.min_v;
				info->max_vertical_frequency = range.max_v;
				info->max_pixel_clock = range.max_clock * 10000;
				break;
			}
			case EDID1_IS_DETAILED_TIMING:
			{
				edid1_detailed_timing& timing = monitor->data.detailed_timing;
				if (timing.h_size > 0) {
					info->width = timing.h_size / 10.0;
					info->height = timing.v_size / 10.0;
				}
				break;
			}
			default:
				break;
		}
	}
	// the names end in a line feed and blanks
	for (char* text = info->name; text != NULL;
			text = text == info->name ? info->serial_number : NULL) {
		size_t end = strlen(text);
		while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == ' '))
			text[--end] = '\0';
	}
	return B_OK;
}


status_t
kms_edid_preferred_size(const uint8* data, size_t length, uint32* width,
	uint32* height)
{
	if (data == NULL || length < sizeof(edid1_raw))
		return B_NOT_SUPPORTED;
	edid1_info edid;
	memset(&edid, 0, sizeof(edid));
	edid_decode(&edid, (const edid1_raw*)data);
	for (uint32 i = 0; i < EDID1_NUM_DETAILED_MONITOR_DESC; ++i) {
		const edid1_detailed_monitor& monitor = edid.detailed_monitor[i];
		if (monitor.monitor_desc_type != EDID1_IS_DETAILED_TIMING)
			continue;
		*width = monitor.data.detailed_timing.h_active;
		*height = monitor.data.detailed_timing.v_active;
		return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}
