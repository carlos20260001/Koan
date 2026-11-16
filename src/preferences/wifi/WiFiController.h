/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef WIFI_CONTROLLER_H
#define WIFI_CONTROLLER_H


#include <Looper.h>
#include <Messenger.h>
#include <NetworkDevice.h>
#include <String.h>

#include <vector>


class BMessageRunner;


// Requests understood by WiFiController
enum {
	kMsgWiFiRefresh			= 'wfRf',
		// Read the current state and report it
	kMsgWiFiScan			= 'wfSc',
		// int64 "max_age": only scan if the last one is older (optional)
	kMsgWiFiJoin			= 'wfJn',
		// string "name", uint32 "authentication" (optional),
		// string "password" (optional), bool "remember", bool "hidden"
	kMsgWiFiLeave			= 'wfLv',
	kMsgWiFiForget			= 'wfFg',
		// string "name"
	kMsgWiFiSetPower		= 'wfPw',
		// bool "on"
	kMsgWiFiSelectDevice	= 'wfSd',
		// string "device"
	kMsgWiFiSetScanInterval	= 'wfSi'
		// int64 "interval" (0 disables periodic scans)
};

// Notifications sent to the controller's target
enum {
	kMsgWiFiStateChanged	= 'wfST',
		// A flattened WiFiState
	kMsgWiFiJoinResult		= 'wfJR'
		// string "name", int32 "status", string "error" (localized)
};

// Sent by WiFiStatus to the WiFi preferences to preselect a network
static const uint32 kMsgWiFiSelectNetwork = 'wnsl';
	// string "name", string "device", uint32 "authentication"
static const uint32 kMsgWiFiJoinOther = 'wjno';

#define kWiFiPreferencesSignature "application/x-vnd.vos-WiFi"


enum wifi_state {
	WIFI_STATE_NO_ADAPTER = 0,
	WIFI_STATE_OFF,
	WIFI_STATE_DISCONNECTED,
	WIFI_STATE_CONNECTING,
	WIFI_STATE_OBTAINING_ADDRESS,
	WIFI_STATE_NO_INTERNET,
	WIFI_STATE_CONNECTED
};


struct WiFiNetworkInfo {
								WiFiNetworkInfo();

			status_t			Archive(BMessage& archive) const;
			status_t			Unarchive(const BMessage& archive);
			bool				operator==(const WiFiNetworkInfo& other) const;
			bool				operator!=(const WiFiNetworkInfo& other) const
									{ return !(*this == other); }

			BString				name;
			uint32				authentication;
			bool				secured;
			int32				dBm;
			int32				percent;
			int32				bars;
			int32				accessPoints;
			bool				connected;
			bool				saved;
			bool				inRange;
};


/*!	Details of the current association for the "Alt-click" view. Empty
	strings and zero values mean the driver does not report the item.
*/
struct WiFiLinkDetails {
								WiFiLinkDetails();

			void				Archive(BMessage& archive) const;
			void				Unarchive(const BMessage& archive);

			BString				bssid;
			BString				address;
			BString				router;
			BString				security;
			BString				phyMode;
			int32				channel;
			int32				frequency;
				// MHz
			int32				dBm;
				// 0 when unknown
			int32				txKbps;
				// Last transmit rate, 0 when the driver does not report it
			BString				txDetails;
				// For example "HE MCS 11, 2 streams, 80 MHz"
};


/*!	Reads the association of the wireless \a device directly from the driver,
	without a WiFiController: for views that only show it, like the Network
	preferences. Returns B_OK and fills in \a link and \a networkName while
	the device is associated and has link.
*/
status_t GetWiFiLinkDetails(const char* device, WiFiLinkDetails& link,
	BString& networkName);


struct WiFiState {
								WiFiState();

			status_t			Archive(BMessage& archive) const;
			status_t			Unarchive(const BMessage& archive);

			const WiFiNetworkInfo* FindNetwork(const char* name) const;
			bool				IsAssociated() const;

			BString				device;
			std::vector<BString> devices;
			int32				state;
			BString				current;
				// The associated or joining network
			BString				pending;
				// A join request in progress
			BString				address;
			bool				scanning;
			bool				savedValid;
			bool				netServerRunning;
			WiFiLinkDetails		link;
				// Valid while associated
			std::vector<WiFiNetworkInfo> networks;
				// In range networks by signal, followed by saved networks
				// that are out of range
};


/*!	Does all adapter and net_server work on its own thread, so that neither
	the WiFi preferences window nor Deskbar ever waits on the network stack,
	net_server or wpa_supplicant. Every net_server request uses timeouts.
*/
class WiFiController : public BLooper {
public:
								WiFiController(const BMessenger& target,
									const char* component,
									bigtime_t scanInterval);
	virtual						~WiFiController();

			status_t			Start();

	virtual	void				MessageReceived(BMessage* message);

private:
			struct Entry {
				wireless_network	network;
				int32				accessPoints;
				bool				secured;
				bigtime_t			lastSeen;
			};

			struct SavedNetwork {
				BString				name;
				uint32				authentication;
			};

			void				_Update(bool scanCompleted);
			bool				_ChooseDevice(bool& changed);
			void				_ClearNetworks();
			void				_ReadNetworks(bool scanCompleted,
									const wireless_network* networks,
									uint32 count,
									const BString& associatedName,
									const wireless_network* associated);
			void				_UpdateSaved(bool force);
			status_t			_StartScan(const char* reason);
			void				_MaybeScan(bool associated);
			void				_CheckJoin(bool associated, bool link,
									const BString& name);
			void				_Join(BMessage* message);
			void				_Leave();
			void				_Forget(const char* name);
			void				_SetPower(bool on);
			void				_AutoJoin();
			void				_FinishJoin(status_t status,
									const char* error);
			void				_PostState();

			const SavedNetwork*	_FindSaved(const char* name) const;
			Entry*				_FindEntry(const char* name);

	static	status_t			_SendToNetServer(BMessage& message,
									BMessage& reply, bigtime_t timeout);

			BMessenger			fTarget;
			BString				fComponent;
			BMessageRunner*		fRunner;
			bool				fWatching;

			BString				fDevice;
			BString				fRequestedDevice;
			std::vector<BString> fDevices;

			std::vector<Entry>	fEntries;
			std::vector<SavedNetwork> fSaved;
			bool				fSavedValid;
			bigtime_t			fSavedUpdated;
			bool				fNetServerRunning;

			bigtime_t			fScanInterval;
			bool				fScanPending;
			bigtime_t			fScanRequested;
			bigtime_t			fLastScan;
			bool				fAutoJoinAfterScan;

			bool				fJoinPending;
			BString				fJoinName;
			uint32				fJoinAuthentication;
			bool				fJoinSecured;
			BString				fJoinPassword;
			bool				fJoinHasPassword;
			bool				fJoinRemember;
			bigtime_t			fJoinStarted;
			bool				fJoinSawOther;

			WiFiState			fState;
			bool				fHasPostedState;
			BString				fLoggedSummary;
};


#endif	// WIFI_CONTROLLER_H
