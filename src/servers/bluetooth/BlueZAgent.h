/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef BLUEZ_AGENT_H
#define BLUEZ_AGENT_H


#include <OS.h>


/*!	The bluetooth_server's role on Linux: BlueZ's pairing agent.

	On Haiku the server owns the radios (HCI), runs the SDP server and the
	HID host, and asks the user for PIN codes and confirmations when a
	device pairs. On Linux BlueZ (bluetoothd) owns the radios, SDP and HID
	(its input plugin, uhid), and the Bluetooth kit talks to it directly
	(BlueZBackend). What BlueZ leaves to the desktop is the agent
	(org.bluez.Agent1): this object registers as the default agent on the
	system bus and asks the user, in air/OS's windows, to enter or confirm
	codes and to allow devices and services.
*/
class BlueZAgent {
public:
								BlueZAgent();
								~BlueZAgent();

			status_t			Start();
			void				Stop();

private:
	static	status_t			_ThreadEntry(void* data);
			void				_Thread();

			thread_id			fThread;
			int					fWakeFD;
			volatile bool		fQuit;
};


#endif	// BLUEZ_AGENT_H
