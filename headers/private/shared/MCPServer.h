/* Copyright 2026 air/OS contributors. Distributed under the MIT License. */
#ifndef _MCP_SERVER_H
#define _MCP_SERVER_H

#include <SupportDefs.h>

static const char* const kMCPServerSignature = "application/x-vnd.vos-mcp_server";
static const uint32 kMCPGetStatus = 'mcpg';
static const uint32 kMCPSetEnabled = 'mcpe';
static const int32 kMCPPort = 7780;

// Local IPC only. SetEnabled takes a bool "enabled". Both messages reply with
// int32 "error", bool "enabled", bool "running" and string "connection_info".
// Connection information (including the credential) is only returned while on.

#endif
