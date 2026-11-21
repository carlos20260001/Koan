# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# air/OS's hpkg package kit (target bpackage, libpackage.so).
Application(
	Repositories

	SOURCES
	AddRepoWindow.cpp
	RepoRow.cpp
	Repositories.cpp
	RepositoriesView.cpp
	RepositoriesWindow.cpp
	RepositoriesSettings.cpp
	TaskLooper.cpp
	TaskTimer.cpp

	LIBS bpackage localestub
	RDEF Repositories.rdef
)

UsePrivateHeaders(Repositories interface)
target_include_directories(Repositories PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/package)
DoCatalogs("x-vnd.vos-Repositories" preferences/repositories)
