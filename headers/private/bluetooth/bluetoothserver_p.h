#ifndef _BLUETOOTH_SERVER_PRIVATE_H
#define _BLUETOOTH_SERVER_PRIVATE_H


#define BLUETOOTH_SIGNATURE "application/x-vnd.vos-bluetooth_server"
#define BLUETOOTH_APP_SIGNATURE "application/x-vnd.vos-BluetoothPrefs"

/* Kit Comunication */
// Watching
#define BT_START_WATCHING_CONNECTIONS               'wtST'
#define BT_STOP_WATCHING_CONNECTIONS                'wtSP'

// LocalDevice
#define BT_MSG_COUNT_LOCAL_DEVICES		'btCd'
#define BT_MSG_ACQUIRE_LOCAL_DEVICE     'btAd'
#define BT_MSG_HANDLE_SIMPLE_REQUEST    'btsR'
#define BT_MSG_ADD_DEVICE               'btDD'
#define BT_MSG_REMOVE_DEVICE            'btrD'
#define BT_MSG_GET_PROPERTY             'btgP'
#define BT_MSG_GET_REMOTE_DEVICES       'btgD'
#define BT_MSG_NEW_REMOTE_DEVICE        'btnD'
#define BT_MSG_PAIR_CONFIRM_RESULT       'btPC'

// LE discovery. Advertising messages carry a raw address and advertisement.
#define BT_MSG_LE_SCAN_START            'leSS'
#define BT_MSG_LE_SCAN_STOP             'leST'
#define BT_MSG_LE_SCAN_STARTED          'leSA'
#define BT_MSG_LE_SCAN_STOPPED          'leSP'
#define BT_MSG_LE_SCAN_ERROR            'leSE'
#define BT_MSG_LE_ADVERTISEMENT         'leAD'

// LE ACL link lifecycle. These messages do not imply pairing or a HID profile.
// Scan stop, connect cancel and disconnect take the client's "listener" so
// one client cannot end another's scan or link; BT_MSG_LE_DISCONNECT also
// accepts bool "force" to end the current link whoever owns it.
#define BT_MSG_LE_CONNECT               'leCN'
#define BT_MSG_LE_CONNECT_CANCEL        'leCC'
#define BT_MSG_LE_DISCONNECT            'leDC'
#define BT_MSG_LE_CONNECTING            'leCG'
#define BT_MSG_LE_CONNECTED             'leCD'
#define BT_MSG_LE_CONNECT_FAILED        'leCF'
#define BT_MSG_LE_DISCONNECTED          'leDD'
// Request: either short_term_key (16 bytes), or long_term_key (16 bytes),
// random_number (8 bytes) and encrypted_diversifier (uint16).
#define BT_MSG_LE_START_ENCRYPTION      'leEN'
// Asks the controller for other parameters on the LE link: "interval_min",
// "interval_max" (1.25 ms units), "latency" (events), "timeout" (10 ms),
// the last two the link's if left out; "keep_interval" (bool) asks for the
// interval again whenever the peer changes it. BT_MSG_LE_CONNECT takes the same four fields (optional) for
// the connection it creates.
#define BT_MSG_LE_UPDATE_CONNECTION     'leUP'
// Logs the signal strength of a link: "handle" (uint16, the LE link if left
// out); the answer goes to the LE log.
#define BT_MSG_READ_RSSI                'leRS'
#define BT_MSG_LE_ENCRYPTED             'leED'
#define BT_MSG_LE_ENCRYPTION_FAILED     'leEF'

// Discovery
#define BT_MSG_INQUIRY_STARTED          'IqSt'
#define BT_MSG_INQUIRY_COMPLETED        'IqCM'
#define BT_MSG_INQUIRY_TERMINATED       'IqTR'
#define BT_MSG_INQUIRY_ERROR            'IqER'
#define BT_MSG_INQUIRY_DEVICE           'IqDE'

// Pairing
#define BT_MSG_CONN_FAILED              'CnFL'
#define BT_MSG_CONN_COMPLETED           'CnCM'
#define BT_MSG_DISCONN_COMPLETED        'DcCM'

#define BT_REQ_CREATE_CONN              'rdCN'
#define BT_REQ_CANCEL_CONN              'rdCC'
#define BT_REQ_DISCONNECT               'rdDC'
#define BT_REQ_REMOVE_DEVICE            'rdRD'

#define BT_REQ_CONN_STATE               'rdCS'


#endif
