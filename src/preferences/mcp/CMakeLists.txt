# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	MCP

	SOURCES
	MCP.cpp

	LIBS localestub
	RDEF MCP.rdef
)

UsePrivateHeaders(MCP shared)
DoCatalogs("x-vnd.vos-MCP" preferences/mcp)
