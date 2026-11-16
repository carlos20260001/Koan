/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "splash_stages.h"

#include <stdlib.h>

#include <SupportDefs.h>
#include <boot_splash.h>


static_assert((int)VOS_SPLASH_STAGE_COUNT == (int)BOOT_SPLASH_STAGE_MAX,
	"one Linux boot point per Haiku splash icon");


int
vos_splash_stage_icon(int stage)
{
	if (stage < VOS_SPLASH_STAGE_INITRAMFS
		|| stage > VOS_SPLASH_STAGE_APP_SERVER)
		return -1;

	// Stage 1 lights BOOT_SPLASH_STAGE_1_INIT_MODULES, the leftmost icon.
	return BOOT_SPLASH_STAGE_1_INIT_MODULES
		+ (stage - VOS_SPLASH_STAGE_INITRAMFS);
}


int
vos_splash_parse_stage(const char* text)
{
	if (text == NULL || text[0] < '0' || text[0] > '9')
		return -1;

	char* end;
	long value = strtol(text, &end, 10);
	while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
		end++;
	if (*end != '\0' || vos_splash_stage_icon((int)value) < 0)
		return -1;
	return (int)value;
}
