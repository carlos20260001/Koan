/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Status Notifier Watcher Integration
 * Monitors system tray / status-notifier-item changes for shelf replicants.
 */

#include <StatusNotifierInterface.h>
#include <StatusView.h>

class StatusNotifierWatcher {
public:
	StatusNotifierWatcher();
	~StatusNotifierWatcher();

	status_t	Watch(const char* serviceName);
	void		UpdateTrayItem(const char* itemName, const char* iconName);
};
