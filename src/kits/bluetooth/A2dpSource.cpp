/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <A2dpSource.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <Autolock.h>
#include <Message.h>

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/HCI/btHCI.h>
#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetooth/RemoteDevice.h>

#include <bluetoothserver_p.h>
#include <l2cap.h>
#include <LELog.h>
#include <sdp.h>


namespace Bluetooth {


// AVDTP 1.3 section 8.4.2: the first octet of every signalling packet.
enum {
	AVDTP_PACKET_SINGLE		= 0,
	AVDTP_PACKET_START		= 1,
	AVDTP_PACKET_CONTINUE	= 2,
	AVDTP_PACKET_END		= 3
};

enum {
	AVDTP_COMMAND			= 0,
	AVDTP_GENERAL_REJECT	= 1,
	AVDTP_ACCEPT			= 2,
	AVDTP_REJECT			= 3
};

// AVDTP 1.3 section 8.5: signal identifiers.
enum {
	AVDTP_DISCOVER				= 0x01,
	AVDTP_GET_CAPABILITIES		= 0x02,
	AVDTP_SET_CONFIGURATION		= 0x03,
	AVDTP_GET_CONFIGURATION		= 0x04,
	AVDTP_RECONFIGURE			= 0x05,
	AVDTP_OPEN					= 0x06,
	AVDTP_START					= 0x07,
	AVDTP_CLOSE					= 0x08,
	AVDTP_SUSPEND				= 0x09,
	AVDTP_ABORT					= 0x0a,
	AVDTP_SECURITY_CONTROL		= 0x0b,
	AVDTP_GET_ALL_CAPABILITIES	= 0x0c,
	AVDTP_DELAY_REPORT			= 0x0d
};

// AVDTP 1.3 section 8.21: service categories.
enum {
	AVDTP_MEDIA_TRANSPORT		= 0x01,
	AVDTP_REPORTING				= 0x02,
	AVDTP_RECOVERY				= 0x03,
	AVDTP_CONTENT_PROTECTION	= 0x04,
	AVDTP_HEADER_COMPRESSION	= 0x05,
	AVDTP_MULTIPLEXING			= 0x06,
	AVDTP_MEDIA_CODEC			= 0x07,
	AVDTP_DELAY_REPORTING		= 0x08
};

// AVDTP 1.3 section 8.20.6 error codes, and A2DP 1.3 section 5.1.3.
enum {
	AVDTP_ERROR_BAD_HEADER_FORMAT		= 0x01,
	AVDTP_ERROR_BAD_LENGTH				= 0x11,
	AVDTP_ERROR_BAD_ACP_SEID			= 0x12,
	AVDTP_ERROR_SEP_IN_USE				= 0x13,
	AVDTP_ERROR_SEP_NOT_IN_USE			= 0x14,
	AVDTP_ERROR_NOT_SUPPORTED_COMMAND	= 0x19,
	AVDTP_ERROR_BAD_STATE				= 0x31
};

// Assigned numbers: media type and codec of the codec capability.
static const uint8 kMediaTypeAudio = 0x00;
static const uint8 kCodecSBC = 0x00;

// Our one stream end point: an SBC audio source.
static const uint8 kLocalSEID = 1;

// SDP UUIDs of the protocols in a protocol descriptor list.
static const uint16 kUUIDL2CAP = 0x0100;
static const uint16 kUUIDAVDTP = 0x0019;

static const bigtime_t kReadTimeout = 500000;
static const size_t kMaxMediaPacket = 2048;


struct A2dpSource::Response {
	uint8	messageType;
	uint8	data[1024];
	size_t	length;
};


static const char*
avdtp_error_string(uint8 error)
{
	switch (error) {
		case AVDTP_ERROR_BAD_HEADER_FORMAT:
			return "bad header format";
		case AVDTP_ERROR_BAD_LENGTH:
			return "bad length";
		case AVDTP_ERROR_BAD_ACP_SEID:
			return "bad stream end point";
		case AVDTP_ERROR_SEP_IN_USE:
			return "stream end point in use";
		case AVDTP_ERROR_SEP_NOT_IN_USE:
			return "stream end point not in use";
		case AVDTP_ERROR_NOT_SUPPORTED_COMMAND:
			return "command not supported";
		case AVDTP_ERROR_BAD_STATE:
			return "bad state";
		case 0x29:
			return "unsupported configuration";
		case 0xc1:
			return "invalid codec type";
		case 0xc3:
			return "invalid sampling frequency";
		case 0xc5:
			return "invalid channel mode";
		case 0xc8:
			return "invalid block length";
		case 0xc9:
			return "invalid subbands";
		case 0xcb:
			return "invalid allocation method";
		case 0xcc:
			return "invalid minimum bitpool";
		case 0xcd:
			return "invalid maximum bitpool";
	}
	return "error";
}


static const char*
address_string(const bdaddr_t& address, char buffer[18])
{
	snprintf(buffer, 18, "%02X:%02X:%02X:%02X:%02X:%02X", address.b[5],
		address.b[4], address.b[3], address.b[2], address.b[1], address.b[0]);
	return buffer;
}


/*!	Opens an L2CAP channel. Needs an ACL link to the device; connect()
	returns once the channel is configured, or fails (the stack times out
	a silent peer).
*/
static status_t
open_l2cap(const bdaddr_t& address, uint16 psm, int& fd)
{
	// TODO(linux backend): air/OS runs AVDTP itself over Haiku's L2CAP
	// sockets. On Linux BlueZ owns AVDTP; an A2DP source registers a media
	// endpoint (org.bluez.Media1.RegisterEndpoint) and streams through the
	// MediaTransport1 file descriptor, or leaves it to PipeWire.
	fd = -1;
	return B_NOT_SUPPORTED;

	fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
	if (fd < 0)
		return errno;

	sockaddr_l2cap remote = {};
	remote.l2cap_len = sizeof(remote);
	remote.l2cap_family = AF_BLUETOOTH;
	remote.l2cap_psm = psm;
	remote.l2cap_bdaddr = address;
	if (connect(fd, (sockaddr*)&remote, sizeof(remote)) < 0) {
		status_t status = errno;
		close(fd);
		fd = -1;
		return status;
	}

	timeval timeout = { 0, kReadTimeout };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	return B_OK;
}


static bool
is_timeout(int error)
{
	return error == B_WOULD_BLOCK || error == B_TIMED_OUT || error == EAGAIN
		|| error == ETIMEDOUT;
}


// #pragma mark - SDP


namespace {

/*!	Reads SDP data elements (Core Vol 3 Part B 3): a header octet with the
	type in the upper five bits and a size index in the lower three.
*/
class DataElement {
public:
	DataElement(const uint8* data, size_t length)
		:
		fData(data),
		fLength(length),
		fValid(false)
	{
		if (length < 1)
			return;
		fType = data[0] >> 3;
		const uint8 sizeIndex = data[0] & 7;
		size_t header = 1;
		size_t size;
		if (fType == 0)
			size = 0;
		else if (sizeIndex < 5)
			size = 1 << sizeIndex;
		else {
			const size_t lengthBytes = 1 << (sizeIndex - 5);
			if (length < 1 + lengthBytes)
				return;
			size = 0;
			for (size_t i = 0; i < lengthBytes; i++)
				size = (size << 8) | data[1 + i];
			header += lengthBytes;
		}
		if (header + size > length)
			return;
		fValue = data + header;
		fSize = size;
		fTotal = header + size;
		fValid = true;
	}

	bool			IsValid() const { return fValid; }
	uint8			Type() const { return fType; }
	const uint8*	Value() const { return fValue; }
	size_t			Size() const { return fSize; }
	size_t			Total() const { return fTotal; }

	bool			IsSequence() const
						{ return fValid && (fType == 6 || fType == 7); }

	uint32 Unsigned() const
	{
		uint32 value = 0;
		for (size_t i = 0; i < fSize && i < 4; i++)
			value = (value << 8) | fValue[i];
		return value;
	}

	// 16-bit form of a UUID, also for 32 and 128-bit ones on the base UUID.
	uint16 UUID() const
	{
		if (fType != 3)
			return 0;
		if (fSize == 2 || fSize == 4)
			return (fValue[fSize - 2] << 8) | fValue[fSize - 1];
		if (fSize == 16)
			return (fValue[2] << 8) | fValue[3];
		return 0;
	}

private:
	const uint8*	fData;
	size_t			fLength;
	bool			fValid;
	uint8			fType;
	const uint8*	fValue;
	size_t			fSize;
	size_t			fTotal;
};


/*!	Iterates over the elements of a sequence. */
class SequenceReader {
public:
	SequenceReader(const DataElement& sequence)
		:
		fData(sequence.Value()),
		fLeft(sequence.IsSequence() ? sequence.Size() : 0)
	{
	}

	bool Next(DataElement& element)
	{
		if (fLeft == 0)
			return false;
		element = DataElement(fData, fLeft);
		if (!element.IsValid()) {
			fLeft = 0;
			return false;
		}
		fData += element.Total();
		fLeft -= element.Total();
		return true;
	}

private:
	const uint8*	fData;
	size_t			fLeft;
};


/*!	Fills \a info from one service record, a sequence of attribute ID and
	value pairs. Returns whether the record is an Audio Sink.
*/
bool
parse_sink_record(const DataElement& record, A2dpSinkInfo& info)
{
	bool isSink = false;
	SequenceReader attributes(record);
	DataElement id(NULL, 0);
	DataElement value(NULL, 0);
	while (attributes.Next(id) && attributes.Next(value)) {
		switch (id.Unsigned()) {
			case SDP_ATTR_SERVICE_CLASS_ID_LIST:
			{
				SequenceReader classes(value);
				DataElement uuid(NULL, 0);
				while (classes.Next(uuid)) {
					if (uuid.UUID() == SDP_SERVICE_CLASS_AUDIO_SINK)
						isSink = true;
				}
				break;
			}

			case SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST:
			{
				// { { L2CAP, PSM }, { AVDTP, version } }
				SequenceReader protocols(value);
				DataElement protocol(NULL, 0);
				while (protocols.Next(protocol)) {
					SequenceReader fields(protocol);
					DataElement uuid(NULL, 0);
					DataElement parameter(NULL, 0);
					if (!fields.Next(uuid) || !fields.Next(parameter))
						continue;
					if (uuid.UUID() == kUUIDL2CAP)
						info.psm = parameter.Unsigned();
					else if (uuid.UUID() == kUUIDAVDTP)
						info.avdtpVersion = parameter.Unsigned();
				}
				break;
			}

			case SDP_ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST:
			{
				SequenceReader profiles(value);
				DataElement profile(NULL, 0);
				while (profiles.Next(profile)) {
					SequenceReader fields(profile);
					DataElement uuid(NULL, 0);
					DataElement version(NULL, 0);
					if (fields.Next(uuid) && fields.Next(version)
						&& uuid.UUID()
							== SDP_SERVICE_CLASS_ADVANCED_AUDIO_DISTRIBUTION)
						info.a2dpVersion = version.Unsigned();
				}
				break;
			}

			case SDP_ATTR_SUPPORTED_FEATURES:
				info.features = value.Unsigned();
				break;
		}
	}
	return isSink;
}

} // namespace


/*static*/ status_t
A2dpSource::QuerySink(const bdaddr_t& address, A2dpSinkInfo& info,
	BString* error)
{
	info.avdtpVersion = 0x0100;
	info.a2dpVersion = 0x0100;
	info.features = 0;
	info.psm = L2CAP_PSM_AVDTP;

	int fd;
	status_t status = open_l2cap(address, L2CAP_PSM_SDP, fd);
	if (status != B_OK) {
		if (error != NULL)
			error->SetToFormat("SDP channel: %s", strerror(status));
		return status;
	}

	// Service Search Attribute Request (Core Vol 3 Part B 4.7): records of
	// class Audio Sink, attributes service class, protocols, profiles and
	// supported features.
	static const uint8 kParameters[] = {
		0x35, 0x03, 0x19, SDP_SERVICE_CLASS_AUDIO_SINK >> 8,
			SDP_SERVICE_CLASS_AUDIO_SINK & 0xff,
		0x02, 0x00,
			// at most 512 bytes of attributes per answer
		0x35, 0x0c,
			0x09, 0x00, 0x01,
			0x09, 0x00, 0x04,
			0x09, 0x00, 0x09,
			0x09, 0x03, 0x11
	};

	uint8 attributes[4096];
	size_t attributesLength = 0;
	uint8 continuation[17] = { 0 };
	uint16 transaction = 1;
	status = B_OK;
	for (int32 round = 0; round < 16; round++) {
		uint8 request[64];
		const size_t parameterLength = sizeof(kParameters) + 1
			+ continuation[0];
		request[0] = SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_REQUEST;
		request[1] = transaction >> 8;
		request[2] = transaction & 0xff;
		request[3] = parameterLength >> 8;
		request[4] = parameterLength & 0xff;
		memcpy(request + 5, kParameters, sizeof(kParameters));
		memcpy(request + 5 + sizeof(kParameters), continuation,
			1 + continuation[0]);
		if (send(fd, request, 5 + parameterLength, 0) < 0) {
			status = errno;
			break;
		}

		uint8 response[1024];
		ssize_t bytes = -1;
		for (int32 tries = 0; tries < 10; tries++) {
			bytes = recv(fd, response, sizeof(response), 0);
			if (bytes >= 0 || !is_timeout(errno))
				break;
		}
		if (bytes < 0) {
			status = errno;
			break;
		}
		if (bytes < 7 || response[0] != SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_RESPONSE
			|| ((response[1] << 8) | response[2]) != transaction) {
			status = B_BAD_DATA;
			break;
		}
		const size_t count = (response[5] << 8) | response[6];
		if (7 + count + 1 > (size_t)bytes
			|| attributesLength + count > sizeof(attributes)) {
			status = B_BAD_DATA;
			break;
		}
		memcpy(attributes + attributesLength, response + 7, count);
		attributesLength += count;

		const uint8* state = response + 7 + count;
		if (state[0] > 16 || 7 + count + 1 + state[0] > (size_t)bytes) {
			status = B_BAD_DATA;
			break;
		}
		memcpy(continuation, state, 1 + state[0]);
		transaction++;
		if (continuation[0] == 0)
			break;
	}
	close(fd);

	if (status != B_OK) {
		if (error != NULL)
			error->SetToFormat("SDP query: %s", strerror(status));
		return status;
	}

	// A sequence of records, each a sequence of attributes.
	bool found = false;
	DataElement records(attributes, attributesLength);
	SequenceReader reader(records);
	DataElement record(NULL, 0);
	while (!found && reader.Next(record))
		found = parse_sink_record(record, info);

	if (!found) {
		if (error != NULL)
			error->SetTo("the device has no Audio Sink service");
		return B_NAME_NOT_FOUND;
	}
	return B_OK;
}


// #pragma mark - A2dpSource


A2dpSource::A2dpSource(const bdaddr_t& address)
	:
	fAddress(address),
	fLock("a2dp source"),
	fSignalSocket(-1),
	fMediaSocket(-1),
	fMediaMTU(0),
	fReader(-1),
	fQuit(false),
	fPending(NULL),
	fPendingLabel(0),
	fPendingSignal(0),
	fNextLabel(0),
	fAssembly(NULL),
	fAssembled(0),
	fState(IDLE),
	fAccepting(false),
	fAwaitingMedia(false),
	fLastSinkCommand(0),
	fRemoteSEID(0),
	fSinkDelayReporting(false),
	fPreferredRate(44100),
	fBitpoolLimit(53),
	fDelayReport(0),
	fSequence(0),
	fTimestamp(0),
	fPageRepetitionMode(0x02),
	fClockOffset(0),
	fDeviceClass(0)
{
	memset(&fSinkInfo, 0, sizeof(fSinkInfo));
	memset(fCapabilities, 0, sizeof(fCapabilities));
	memset(&fConfiguration, 0, sizeof(fConfiguration));
	fResponseSem = create_sem(0, "a2dp response");
}


A2dpSource::~A2dpSource()
{
	Disconnect();
	delete_sem(fResponseSem);
	free(fAssembly);
}


void
A2dpSource::SetTarget(const BMessenger& target)
{
	BAutolock _(fLock);
	fTarget = target;
}


bool
A2dpSource::IsOpen() const
{
	return fState == OPEN || fState == STREAMING;
}


bool
A2dpSource::IsStreaming() const
{
	return fState == STREAMING;
}


status_t
A2dpSource::_Fail(status_t status, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	char buffer[256];
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	fError = buffer;
	LELog(LE_LOG_ERROR, "a2dp", "%s", buffer);
	return status != B_OK ? status : B_ERROR;
}


status_t
A2dpSource::Connect(bigtime_t timeout)
{
	if (fState != IDLE)
		return B_BUSY;

	const bigtime_t deadline = system_time() + timeout;
	char address[18];
	LELog(LE_LOG_INFO, "a2dp", "connecting to %s",
		address_string(fAddress, address));

	status_t status = _EnsureLink(deadline);
	if (status != B_OK)
		return status;

	BString error;
	status = QuerySink(fAddress, fSinkInfo, &error);
	if (status == B_NAME_NOT_FOUND)
		return _Fail(status, "%s", error.String());
	if (status != B_OK) {
		// Some devices answer SDP badly; AVDTP has a fixed PSM anyway.
		LELog(LE_LOG_INFO, "a2dp", "%s; trying AVDTP anyway",
			error.String());
	} else {
		LELog(LE_LOG_INFO, "a2dp", "sink: A2DP %x.%02x, AVDTP %x.%02x, "
			"features %#x", fSinkInfo.a2dpVersion >> 8,
			fSinkInfo.a2dpVersion & 0xff, fSinkInfo.avdtpVersion >> 8,
			fSinkInfo.avdtpVersion & 0xff, fSinkInfo.features);
	}

	status = open_l2cap(fAddress, L2CAP_PSM_AVDTP, fSignalSocket);
	if (status != B_OK)
		return _Fail(status, "AVDTP signalling channel: %s", strerror(status));

	fQuit = false;
	fReader = spawn_thread(&_ReaderEntry, "a2dp signalling",
		B_URGENT_DISPLAY_PRIORITY, this);
	if (fReader < 0) {
		close(fSignalSocket);
		fSignalSocket = -1;
		return _Fail(fReader, "no signalling thread");
	}
	resume_thread(fReader);

	status = _Discover();
	if (status == B_OK)
		status = _Configure();
	if (status == B_OK)
		status = _Open();
	if (status != B_OK) {
		Disconnect();
		return status;
	}
	return B_OK;
}


status_t
A2dpSource::Accept(int signalSocket, bigtime_t timeout)
{
	if (fState != IDLE || fSignalSocket >= 0) {
		close(signalSocket);
		return B_BUSY;
	}
	const bigtime_t deadline = system_time() + timeout;
	char address[18];
	LELog(LE_LOG_INFO, "a2dp", "%s opened the signalling channel",
		address_string(fAddress, address));

	timeval readTimeout = { 0, kReadTimeout };
	setsockopt(signalSocket, SOL_SOCKET, SO_RCVTIMEO, &readTimeout,
		sizeof(readTimeout));
	fSignalSocket = signalSocket;
	fAccepting = true;
	fAwaitingMedia = false;
	fLastSinkCommand = 0;
	fQuit = false;
	fReader = spawn_thread(&_ReaderEntry, "a2dp signalling",
		B_URGENT_DISPLAY_PRIORITY, this);
	if (fReader < 0) {
		fAccepting = false;
		close(fSignalSocket);
		fSignalSocket = -1;
		return _Fail(fReader, "no signalling thread");
	}
	resume_thread(fReader);

	// A sink that opens the channel usually sets the stream up as well;
	// one that has said nothing for a while waits for us to.
	const bigtime_t opened = system_time();
	status_t status = B_TIMED_OUT;
	while (system_time() < deadline) {
		if (fState == OPEN || fState == STREAMING) {
			status = B_OK;
			break;
		}
		thread_info reader;
		if (get_thread_info(fReader, &reader) != B_OK) {
			// the sink closed the signalling channel again
			status = B_DEV_NOT_READY;
			break;
		}
		const bigtime_t quietSince = fLastSinkCommand != 0
			? fLastSinkCommand : opened;
		if (fState == IDLE && system_time() - quietSince > 1500000) {
			LELog(LE_LOG_INFO, "a2dp", "the sink waits; setting the stream "
				"up ourselves");
			fAccepting = false;
			status = _Discover();
			if (status == B_OK)
				status = _Configure();
			if (status == B_OK)
				status = _Open();
			break;
		}
		if (fState == CONFIGURED && !fAwaitingMedia
			&& system_time() - quietSince > 1500000) {
			// configured, but not opened: open it ourselves
			fAccepting = false;
			status = _Open();
			break;
		}
		snooze(20000);
	}
	fAccepting = false;
	fAwaitingMedia = false;
	if (status != B_OK) {
		if (status == B_TIMED_OUT)
			_Fail(status, "the sink did not finish setting the stream up");
		else if (status == B_DEV_NOT_READY)
			_Fail(status, "the sink closed the signalling channel");
		Disconnect();
		return status;
	}
	return B_OK;
}


status_t
A2dpSource::AdoptMediaChannel(int socket)
{
	uint16 mtu = 0;
	socklen_t size = sizeof(mtu);
	if (getsockopt(socket, BLUETOOTH_PROTO_L2CAP, B_L2CAP_OUTGOING_MTU, &mtu,
			&size) != 0 || mtu < 48)
		mtu = L2CAP_MTU_DEFAULT;

	BAutolock _(fLock);
	if (!fAwaitingMedia || fMediaSocket >= 0 || fState != CONFIGURED)
		return B_NOT_ALLOWED;
	fMediaSocket = socket;
	fMediaMTU = mtu;
	fAwaitingMedia = false;
	fState = OPEN;
	LELog(LE_LOG_INFO, "a2dp", "stream open (the sink's), media packets up "
		"to %u bytes", mtu);
	return B_OK;
}


status_t
A2dpSource::_LinkState(bool& connected, bool& encrypted)
{
	BMessenger server(BLUETOOTH_SIGNATURE);
	BMessage request(BT_MSG_ACQUIRE_LOCAL_DEVICE);
	BMessage reply;
	hci_id hid = -1;
	if (server.SendMessage(&request, &reply) != B_OK
		|| reply.FindInt32("hci_id", &hid) != B_OK || hid < 0)
		return B_DEV_NOT_READY;

	request.MakeEmpty();
	request.what = BT_REQ_CONN_STATE;
	request.AddInt32("hci_id", hid);
	request.AddData("bdaddr", B_ANY_TYPE, &fAddress, sizeof(bdaddr_t));
	reply.MakeEmpty();
	uint8 state;
	if (server.SendMessage(&request, &reply) != B_OK
		|| reply.FindUInt8("conn state", &state) != B_OK)
		return B_ERROR;

	connected = state == RemoteDevice::CONNECTED;
	encrypted = connected && reply.GetBool("encrypted", false);
	return B_OK;
}


/*!	Makes sure there is an encrypted ACL link to the sink. A new link is
	authenticated by the bluetooth_server, pairing first if there is no key
	(which may ask the user to confirm), then encrypted.
*/
status_t
A2dpSource::_EnsureLink(bigtime_t deadline)
{
	BMessenger server(BLUETOOTH_SIGNATURE);
	if (!server.IsValid())
		return _Fail(B_DEV_NOT_READY, "the Bluetooth server is not running");

	BMessage request(BT_MSG_ACQUIRE_LOCAL_DEVICE);
	BMessage reply;
	hci_id hid = -1;
	if (server.SendMessage(&request, &reply) != B_OK
		|| reply.FindInt32("hci_id", &hid) != B_OK || hid < 0)
		return _Fail(B_DEV_NOT_READY, "no Bluetooth controller");

	bool connected = false;
	bool encrypted = false;
	if (_LinkState(connected, encrypted) == B_OK && encrypted)
		return B_OK;

	if (!connected) {
		request.MakeEmpty();
		request.what = BT_REQ_CREATE_CONN;
		request.AddInt32("hci_id", hid);
		request.AddData("bdaddr", B_ANY_TYPE, &fAddress, sizeof(bdaddr_t));
		char address[18];
		request.AddString("name", address_string(fAddress, address));
		request.AddUInt32("record", fDeviceClass);

		uint16 packetType = HCI_DM1 | HCI_DH1 | HCI_DM3 | HCI_DH3 | HCI_DM5
			| HCI_DH5;
		BMessage property(BT_MSG_GET_PROPERTY);
		property.AddInt32("hci_id", hid);
		property.AddString("property", "packet_type");
		reply.MakeEmpty();
		int32 value;
		if (server.SendMessage(&property, &reply) == B_OK
			&& reply.FindInt32("result", &value) == B_OK && value != 0)
			packetType = value;
		request.AddUInt16("packet type", packetType);
		request.AddUInt8("pscan_rep_mode", fPageRepetitionMode);
			// R2 unless an inquiry told better
		request.AddUInt8("pscan_mode", 0);
		request.AddUInt16("clock_offset", fClockOffset);
		request.AddUInt8("role_switch", 1);
		reply.MakeEmpty();
		if (server.SendMessage(&request, &reply) != B_OK)
			return _Fail(B_ERROR, "could not ask for a connection");
		LELog(LE_LOG_INFO, "a2dp", "asked for an ACL link");
	}

	// The server pairs or authenticates, then turns on encryption.
	bool wasConnected = connected;
	const bigtime_t start = system_time();
	while (system_time() < deadline) {
		snooze(200000);
		if (_LinkState(connected, encrypted) != B_OK)
			continue;
		if (encrypted) {
			LELog(LE_LOG_INFO, "a2dp", "link encrypted after %" B_PRId64
				" ms", (system_time() - start) / 1000);
			return B_OK;
		}
		if (connected)
			wasConnected = true;
		else if (wasConnected || system_time() - start > 15000000) {
			// The link came and went (pairing refused, or the device
			// dropped it), or it never came up.
			return _Fail(EHOSTUNREACH, "the device %s",
				wasConnected ? "dropped the link (pairing failed?)"
					: "did not answer");
		}
	}
	return _Fail(B_TIMED_OUT, "the link was not %s in time",
		connected ? "encrypted" : "made");
}


/*static*/ status_t
A2dpSource::_ReaderEntry(void* cookie)
{
	((A2dpSource*)cookie)->_Reader();
	return B_OK;
}


void
A2dpSource::_Reader()
{
	uint8 buffer[2048];
	while (!fQuit) {
		ssize_t bytes = recv(fSignalSocket, buffer, sizeof(buffer), 0);
		if (bytes < 0) {
			if (is_timeout(errno))
				continue;
			break;
		}
		if (bytes > 0)
			_HandleMessage(buffer, bytes);
	}

	BAutolock _(fLock);
	if (fPending != NULL) {
		fPending->messageType = 0xff;
		fPending = NULL;
		release_sem(fResponseSem);
	}
	if (!fQuit) {
		LELog(LE_LOG_INFO, "a2dp", "the sink closed the signalling channel");
		fState = IDLE;
		_Notify(B_A2DP_STREAM_CLOSED);
	}
}


void
A2dpSource::_HandleMessage(const uint8* data, size_t length)
{
	if (length < 1)
		return;

	const uint8 label = data[0] >> 4;
	const uint8 packetType = (data[0] >> 2) & 3;
	const uint8 messageType = data[0] & 3;

	uint8 signal;
	const uint8* parameters;
	size_t parameterLength;
	switch (packetType) {
		case AVDTP_PACKET_SINGLE:
			if (length < 2)
				return;
			signal = data[1] & 0x3f;
			parameters = data + 2;
			parameterLength = length - 2;
			break;

		case AVDTP_PACKET_START:
			// AVDTP 1.3 section 8.4.3: number of packets, then the signal.
			if (length < 3)
				return;
			free(fAssembly);
			fAssembly = (uint8*)malloc(length - 3);
			if (fAssembly == NULL && length > 3)
				return;
			memcpy(fAssembly, data + 3, length - 3);
			fAssembled = length - 3;
			fAssemblySignal = data[2] & 0x3f;
			fAssemblyLabel = label;
			fAssemblyType = messageType;
			return;

		case AVDTP_PACKET_CONTINUE:
		case AVDTP_PACKET_END:
		{
			if (fAssembly == NULL || label != fAssemblyLabel)
				return;
			uint8* grown = (uint8*)realloc(fAssembly,
				fAssembled + length - 1);
			if (grown == NULL)
				return;
			fAssembly = grown;
			memcpy(fAssembly + fAssembled, data + 1, length - 1);
			fAssembled += length - 1;
			if (packetType == AVDTP_PACKET_CONTINUE)
				return;

			uint8* assembled = fAssembly;
			fAssembly = NULL;
			if (fAssemblyType == AVDTP_COMMAND) {
				_HandleCommand(label, fAssemblySignal, assembled, fAssembled);
			} else {
				uint8 header[2] = { (uint8)((label << 4) | fAssemblyType),
					fAssemblySignal };
				uint8* single = (uint8*)malloc(fAssembled + 2);
				if (single != NULL) {
					memcpy(single, header, 2);
					memcpy(single + 2, assembled, fAssembled);
					_HandleMessage(single, fAssembled + 2);
					free(single);
				}
			}
			free(assembled);
			return;
		}

		default:
			return;
	}

	if (messageType == AVDTP_COMMAND) {
		_HandleCommand(label, signal, parameters, parameterLength);
		return;
	}

	BAutolock _(fLock);
	if (fPending == NULL || label != fPendingLabel
		|| (messageType != AVDTP_GENERAL_REJECT && signal != fPendingSignal)) {
		LELog(LE_LOG_DEBUG, "a2dp", "unexpected response, signal %#x label "
			"%u", signal, label);
		return;
	}

	fPending->messageType = messageType;
	fPending->length = min_c(parameterLength, sizeof(fPending->data));
	memcpy(fPending->data, parameters, fPending->length);
	fPending = NULL;
	release_sem(fResponseSem);
}


/*!	Answers what the sink asks on its own. We have one stream end point,
	an SBC source.
*/
void
A2dpSource::_HandleCommand(uint8 label, uint8 signal, const uint8* data,
	size_t length)
{
	LELog(LE_LOG_DEBUG, "a2dp", "sink sent signal %#x", signal);
	fLastSinkCommand = system_time();

	const uint8 seid = length > 0 ? data[0] >> 2 : 0;
	uint8 reply[32];
	size_t replyLength = 0;
	uint8 type = AVDTP_ACCEPT;
	uint32 notify = 0;

	switch (signal) {
		case AVDTP_DISCOVER:
			// AVDTP 1.3 section 8.6.2: SEID, in use; media type, TSEP.
			reply[0] = (kLocalSEID << 2) | (fState != IDLE ? 0x02 : 0);
			reply[1] = (kMediaTypeAudio << 4) | (0 << 3);
			replyLength = 2;
			break;

		case AVDTP_GET_CAPABILITIES:
		case AVDTP_GET_ALL_CAPABILITIES:
			if (seid != kLocalSEID) {
				type = AVDTP_REJECT;
				reply[0] = AVDTP_ERROR_BAD_ACP_SEID;
				replyLength = 1;
				break;
			}
			reply[0] = AVDTP_MEDIA_TRANSPORT;
			reply[1] = 0;
			reply[2] = AVDTP_MEDIA_CODEC;
			reply[3] = 6;
			reply[4] = kMediaTypeAudio << 4;
			reply[5] = kCodecSBC;
			reply[6] = SBC_FREQUENCY_44100 | SBC_FREQUENCY_48000
				| SBC_MODE_MONO | SBC_MODE_DUAL_CHANNEL | SBC_MODE_STEREO
				| SBC_MODE_JOINT_STEREO;
			reply[7] = SBC_BLOCKS_4 | SBC_BLOCKS_8 | SBC_BLOCKS_12
				| SBC_BLOCKS_16 | SBC_SUBBANDS_4 | SBC_SUBBANDS_8
				| SBC_ALLOCATION_SNR | SBC_ALLOCATION_LOUDNESS;
			reply[8] = 2;
			reply[9] = 53;
			replyLength = 10;
			break;

		case AVDTP_GET_CONFIGURATION:
			if (fState == IDLE || seid != kLocalSEID) {
				type = AVDTP_REJECT;
				reply[0] = fState == IDLE ? AVDTP_ERROR_SEP_NOT_IN_USE
					: AVDTP_ERROR_BAD_ACP_SEID;
				replyLength = 1;
				break;
			}
			reply[0] = AVDTP_MEDIA_TRANSPORT;
			reply[1] = 0;
			reply[2] = AVDTP_MEDIA_CODEC;
			reply[3] = 6;
			reply[4] = kMediaTypeAudio << 4;
			reply[5] = kCodecSBC;
			reply[6] = (fConfiguration.sampleRate == 48000
					? SBC_FREQUENCY_48000 : SBC_FREQUENCY_44100)
				| fConfiguration.channelMode;
			reply[7] = (fConfiguration.blocks == 4 ? SBC_BLOCKS_4
					: fConfiguration.blocks == 8 ? SBC_BLOCKS_8
					: fConfiguration.blocks == 12 ? SBC_BLOCKS_12
					: SBC_BLOCKS_16)
				| (fConfiguration.subbands == 4 ? SBC_SUBBANDS_4
					: SBC_SUBBANDS_8)
				| fConfiguration.allocation;
			reply[8] = fConfiguration.minBitpool;
			reply[9] = fConfiguration.bitpool;
			replyLength = 10;
			break;

		case AVDTP_SET_CONFIGURATION:
		{
			// Only a sink that opened the signalling channel itself sets
			// the stream up; otherwise we do.
			uint8 category, error;
			if (fAccepting && fState == IDLE && seid == kLocalSEID
				&& _TakeConfiguration(data, length, category, error)) {
				LELog(LE_LOG_INFO, "a2dp", "the sink configured the stream");
				_LogConfiguration();
				break;
			}
			type = AVDTP_REJECT;
			if (!fAccepting || fState != IDLE) {
				category = 0;
				error = fState != IDLE ? AVDTP_ERROR_SEP_IN_USE
					: AVDTP_ERROR_NOT_SUPPORTED_COMMAND;
			} else if (seid != kLocalSEID) {
				category = 0;
				error = AVDTP_ERROR_BAD_ACP_SEID;
			}
			reply[0] = category;
			reply[1] = error;
			replyLength = 2;
			break;
		}

		case AVDTP_RECONFIGURE:
			type = AVDTP_REJECT;
			reply[0] = 0;
			reply[1] = AVDTP_ERROR_NOT_SUPPORTED_COMMAND;
			replyLength = 2;
			break;

		case AVDTP_OPEN:
			// The sink opens the transport channel next (AVDTP 1.3
			// section 6.11).
			if (fAccepting && fState == CONFIGURED && seid == kLocalSEID) {
				fAwaitingMedia = true;
				break;
			}
			type = AVDTP_REJECT;
			reply[0] = AVDTP_ERROR_BAD_STATE;
			replyLength = 1;
			break;

		case AVDTP_START:
		{
			BAutolock _(fLock);
			if (fState == OPEN) {
				fState = STREAMING;
				notify = B_A2DP_STREAM_STARTED;
			} else if (fState != STREAMING) {
				type = AVDTP_REJECT;
				reply[0] = seid << 2;
				reply[1] = AVDTP_ERROR_BAD_STATE;
				replyLength = 2;
			}
			break;
		}

		case AVDTP_SUSPEND:
		{
			BAutolock _(fLock);
			if (fState == STREAMING) {
				fState = OPEN;
				notify = B_A2DP_STREAM_SUSPENDED;
			} else if (fState != OPEN) {
				type = AVDTP_REJECT;
				reply[0] = seid << 2;
				reply[1] = AVDTP_ERROR_BAD_STATE;
				replyLength = 2;
			}
			break;
		}

		case AVDTP_CLOSE:
		case AVDTP_ABORT:
		{
			BAutolock _(fLock);
			if (fState != IDLE) {
				fState = IDLE;
				notify = B_A2DP_STREAM_CLOSED;
			}
			break;
		}

		case AVDTP_DELAY_REPORT:
			// AVDTP 1.3 section 8.19: SEID, delay in 1/10 ms, big endian.
			if (length >= 3) {
				fDelayReport = (data[1] << 8) | data[2];
				LELog(LE_LOG_DEBUG, "a2dp", "sink delay %u.%u ms",
					fDelayReport / 10, fDelayReport % 10);
			}
			break;

		default:
			type = AVDTP_GENERAL_REJECT;
			break;
	}

	_Send(fSignalSocket, label, type, signal, reply, replyLength);

	if (notify == B_A2DP_STREAM_CLOSED) {
		LELog(LE_LOG_INFO, "a2dp", "the sink closed the stream");
		_CloseMedia();
	} else if (notify != 0) {
		LELog(LE_LOG_INFO, "a2dp", "the sink %s the stream",
			notify == B_A2DP_STREAM_STARTED ? "started" : "suspended");
	}
	if (notify != 0)
		_Notify(notify);
}


void
A2dpSource::_Notify(uint32 what)
{
	BMessage message(what);
	fTarget.SendMessage(&message);
}


status_t
A2dpSource::_Send(int socket, uint8 label, uint8 messageType, uint8 signal,
	const void* data, size_t length)
{
	// Our messages are short; they always fit in a single packet.
	uint8 packet[64];
	if (length + 2 > sizeof(packet))
		return B_BAD_VALUE;
	packet[0] = (label << 4) | (AVDTP_PACKET_SINGLE << 2) | messageType;
	packet[1] = signal & 0x3f;
	if (length > 0)
		memcpy(packet + 2, data, length);
	if (send(socket, packet, length + 2, 0) < 0)
		return errno;
	return B_OK;
}


status_t
A2dpSource::_Command(uint8 signal, const void* data, size_t length,
	Response* response, bigtime_t timeout)
{
	uint8 label;
	{
		BAutolock _(fLock);
		if (fSignalSocket < 0)
			return B_NOT_ALLOWED;
		label = fNextLabel;
		fNextLabel = (fNextLabel + 1) & 0x0f;
		fPending = response;
		fPendingLabel = label;
		fPendingSignal = signal;
	}

	status_t status = _Send(fSignalSocket, label, AVDTP_COMMAND, signal,
		data, length);
	if (status == B_OK) {
		status = acquire_sem_etc(fResponseSem, 1, B_RELATIVE_TIMEOUT,
			timeout);
	}

	{
		BAutolock _(fLock);
		if (status != B_OK && fPending == NULL) {
			// The answer came just as we gave up on it.
			acquire_sem_etc(fResponseSem, 1, B_RELATIVE_TIMEOUT, 0);
			status = B_OK;
		}
		fPending = NULL;
	}
	if (status != B_OK)
		return status == B_TIMED_OUT ? B_TIMED_OUT : status;

	switch (response->messageType) {
		case AVDTP_ACCEPT:
			return B_OK;
		case AVDTP_GENERAL_REJECT:
			return B_NOT_SUPPORTED;
		case AVDTP_REJECT:
			return B_NOT_ALLOWED;
		default:
			return B_IO_ERROR;
	}
}


status_t
A2dpSource::_Discover()
{
	Response response;
	status_t status = _Command(AVDTP_DISCOVER, NULL, 0, &response);
	if (status != B_OK)
		return _Fail(status, "AVDTP discover: %s", strerror(status));

	// Find an SBC audio sink that is free. Each end point is two octets:
	// SEID, in use; media type, TSEP (AVDTP 1.3 section 8.6.2).
	for (size_t i = 0; i + 1 < response.length; i += 2) {
		const uint8 seid = response.data[i] >> 2;
		const bool inUse = (response.data[i] & 0x02) != 0;
		const uint8 mediaType = response.data[i + 1] >> 4;
		const bool sink = (response.data[i + 1] & 0x08) != 0;
		LELog(LE_LOG_INFO, "a2dp", "end point %u: %s %s%s", seid,
			mediaType == kMediaTypeAudio ? "audio" : "other",
			sink ? "sink" : "source", inUse ? ", in use" : "");
		if (!sink || mediaType != kMediaTypeAudio || inUse)
			continue;

		uint8 parameter = seid << 2;
		Response capabilities;
		status = B_NOT_SUPPORTED;
		if (fSinkInfo.avdtpVersion >= 0x0103) {
			status = _Command(AVDTP_GET_ALL_CAPABILITIES, &parameter, 1,
				&capabilities);
		}
		if (status == B_NOT_SUPPORTED || status == B_NOT_ALLOWED) {
			status = _Command(AVDTP_GET_CAPABILITIES, &parameter, 1,
				&capabilities);
		}
		if (status != B_OK) {
			LELog(LE_LOG_INFO, "a2dp", "capabilities of %u: %s", seid,
				strerror(status));
			continue;
		}

		// Service capabilities: category, length, contents.
		bool sbc = false;
		for (size_t offset = 0; offset + 2 <= capabilities.length;) {
			const uint8 category = capabilities.data[offset];
			const uint8 size = capabilities.data[offset + 1];
			const uint8* item = capabilities.data + offset + 2;
			if (offset + 2 + size > capabilities.length)
				break;
			if (category == AVDTP_MEDIA_CODEC && size >= 6
				&& (item[0] >> 4) == kMediaTypeAudio && item[1] == kCodecSBC) {
				memcpy(fCapabilities, item + 2, 4);
				sbc = true;
			} else if (category == AVDTP_DELAY_REPORTING)
				fSinkDelayReporting = true;
			offset += 2 + size;
		}
		if (!sbc) {
			LELog(LE_LOG_INFO, "a2dp", "end point %u has no SBC", seid);
			continue;
		}

		LELog(LE_LOG_INFO, "a2dp", "using end point %u, SBC capabilities "
			"%02x %02x, bitpool %u-%u", seid, fCapabilities[0],
			fCapabilities[1], fCapabilities[2], fCapabilities[3]);
		fRemoteSEID = seid;
		return B_OK;
	}

	return _Fail(B_NAME_NOT_FOUND, "the device has no free SBC audio sink");
}


status_t
A2dpSource::_Configure()
{
	const uint8 frequencies = fCapabilities[0] & 0xf0;
	const uint8 modes = fCapabilities[0] & 0x0f;
	const uint8 blocks = fCapabilities[1] & 0xf0;
	const uint8 subbands = fCapabilities[1] & 0x0c;
	const uint8 allocations = fCapabilities[1] & 0x03;

	SbcConfiguration& config = fConfiguration;
	uint8 frequency;
	if (fPreferredRate == 48000 && (frequencies & SBC_FREQUENCY_48000) != 0)
		frequency = SBC_FREQUENCY_48000;
	else if ((frequencies & SBC_FREQUENCY_44100) != 0)
		frequency = SBC_FREQUENCY_44100;
	else if ((frequencies & SBC_FREQUENCY_48000) != 0)
		frequency = SBC_FREQUENCY_48000;
	else
		return _Fail(B_NOT_SUPPORTED, "the sink takes neither 44.1 nor 48 kHz");
	config.sampleRate = frequency == SBC_FREQUENCY_48000 ? 48000 : 44100;

	if ((modes & SBC_MODE_JOINT_STEREO) != 0)
		config.channelMode = SBC_MODE_JOINT_STEREO;
	else if ((modes & SBC_MODE_STEREO) != 0)
		config.channelMode = SBC_MODE_STEREO;
	else if ((modes & SBC_MODE_DUAL_CHANNEL) != 0)
		config.channelMode = SBC_MODE_DUAL_CHANNEL;
	else if ((modes & SBC_MODE_MONO) != 0)
		config.channelMode = SBC_MODE_MONO;
	else
		return _Fail(B_NOT_SUPPORTED, "the sink has no channel mode");

	uint8 blockBit;
	if ((blocks & SBC_BLOCKS_16) != 0)
		config.blocks = 16, blockBit = SBC_BLOCKS_16;
	else if ((blocks & SBC_BLOCKS_12) != 0)
		config.blocks = 12, blockBit = SBC_BLOCKS_12;
	else if ((blocks & SBC_BLOCKS_8) != 0)
		config.blocks = 8, blockBit = SBC_BLOCKS_8;
	else if ((blocks & SBC_BLOCKS_4) != 0)
		config.blocks = 4, blockBit = SBC_BLOCKS_4;
	else
		return _Fail(B_NOT_SUPPORTED, "the sink has no block length");

	uint8 subbandBit;
	if ((subbands & SBC_SUBBANDS_8) != 0)
		config.subbands = 8, subbandBit = SBC_SUBBANDS_8;
	else if ((subbands & SBC_SUBBANDS_4) != 0)
		config.subbands = 4, subbandBit = SBC_SUBBANDS_4;
	else
		return _Fail(B_NOT_SUPPORTED, "the sink has no subband count");

	if ((allocations & SBC_ALLOCATION_LOUDNESS) != 0)
		config.allocation = SBC_ALLOCATION_LOUDNESS;
	else if ((allocations & SBC_ALLOCATION_SNR) != 0)
		config.allocation = SBC_ALLOCATION_SNR;
	else
		return _Fail(B_NOT_SUPPORTED, "the sink has no allocation method");

	// The "high quality" bitpools of A2DP 1.3 table 4.7, within what the
	// sink takes and what the SBC frame allows.
	uint32 bitpool;
	if (config.channelMode == SBC_MODE_MONO
		|| config.channelMode == SBC_MODE_DUAL_CHANNEL)
		bitpool = config.sampleRate == 48000 ? 29 : 31;
	else
		bitpool = config.sampleRate == 48000 ? 51 : 53;
	bitpool = min_c(bitpool, (uint32)fBitpoolLimit);
	bitpool = min_c(bitpool, (uint32)fCapabilities[3]);
	const uint32 frameLimit = (config.channelMode == SBC_MODE_MONO
		|| config.channelMode == SBC_MODE_DUAL_CHANNEL ? 16 : 32)
			* config.subbands;
	bitpool = min_c(bitpool, frameLimit);
	if (bitpool < fCapabilities[2] || bitpool < 2) {
		return _Fail(B_NOT_SUPPORTED, "no usable bitpool (sink %u-%u)",
			fCapabilities[2], fCapabilities[3]);
	}
	config.bitpool = bitpool;
	config.minBitpool = max_c(fCapabilities[2], (uint8)2);
	config.maxBitpool = fCapabilities[3];

	// AVDTP 1.3 section 8.9: remote and local SEID, then the capabilities
	// the stream uses.
	uint8 parameters[16];
	size_t length = 0;
	parameters[length++] = fRemoteSEID << 2;
	parameters[length++] = kLocalSEID << 2;
	parameters[length++] = AVDTP_MEDIA_TRANSPORT;
	parameters[length++] = 0;
	parameters[length++] = AVDTP_MEDIA_CODEC;
	parameters[length++] = 6;
	parameters[length++] = kMediaTypeAudio << 4;
	parameters[length++] = kCodecSBC;
	parameters[length++] = frequency | config.channelMode;
	parameters[length++] = blockBit | subbandBit | config.allocation;
	parameters[length++] = config.minBitpool;
	parameters[length++] = config.bitpool;
	if (fSinkDelayReporting) {
		parameters[length++] = AVDTP_DELAY_REPORTING;
		parameters[length++] = 0;
	}

	Response response;
	status_t status = _Command(AVDTP_SET_CONFIGURATION, parameters, length,
		&response);
	if (status == B_NOT_ALLOWED && response.length >= 2) {
		return _Fail(status, "the sink refused the configuration: %s "
			"(category %u)", avdtp_error_string(response.data[1]),
			response.data[0]);
	}
	if (status != B_OK)
		return _Fail(status, "AVDTP set configuration: %s", strerror(status));

	{
		BAutolock _(fLock);
		fState = CONFIGURED;
	}
	_LogConfiguration();
	return B_OK;
}


void
A2dpSource::_LogConfiguration()
{
	const SbcConfiguration& config = fConfiguration;
	LELog(LE_LOG_INFO, "a2dp", "configured SBC %" B_PRIu32 " Hz, mode %#x, "
		"%u blocks, %u subbands, %s, bitpool %u: %" B_PRIuSIZE " byte frames, "
		"%" B_PRIu32 " kbit/s", config.sampleRate, config.channelMode,
		config.blocks, config.subbands,
		config.allocation == SBC_ALLOCATION_LOUDNESS ? "loudness" : "SNR",
		config.bitpool, config.FrameSize(), config.BitRate() / 1000);
}


/*!	The sink's SET_CONFIGURATION (AVDTP 1.3 section 8.9.1): ACP and INT
	SEID, then service capabilities. Takes an SBC configuration with one
	choice per field that the encoder can do; otherwise names the category
	and error to reject it with.
*/
bool
A2dpSource::_TakeConfiguration(const uint8* data, size_t length,
	uint8& category, uint8& error)
{
	category = 0;
	error = AVDTP_ERROR_BAD_LENGTH;
	if (length < 2)
		return false;
	const uint8 remote = data[1] >> 2;
	bool haveCodec = false;
	bool delayReporting = false;
	SbcConfiguration config = {};
	for (size_t offset = 2; offset + 2 <= length;) {
		category = data[offset];
		const uint8 size = data[offset + 1];
		const uint8* value = data + offset + 2;
		if (offset + 2 + size > length) {
			error = AVDTP_ERROR_BAD_LENGTH;
			return false;
		}
		offset += 2 + size;
		if (category == AVDTP_MEDIA_TRANSPORT)
			continue;
		if (category == AVDTP_DELAY_REPORTING) {
			delayReporting = true;
			continue;
		}
		if (category != AVDTP_MEDIA_CODEC) {
			error = AVDTP_ERROR_NOT_SUPPORTED_COMMAND;
			return false;
		}
		// media type, codec, then the SBC element: one bit per field
		if (size != 6 || (value[0] >> 4) != kMediaTypeAudio
			|| value[1] != kCodecSBC) {
			error = AVDTP_ERROR_NOT_SUPPORTED_COMMAND;
			return false;
		}
		const uint8 frequency = value[2] & 0xf0;
		const uint8 mode = value[2] & 0x0f;
		const uint8 blocks = value[3] & 0xf0;
		const uint8 subbands = value[3] & 0x0c;
		const uint8 allocation = value[3] & 0x03;
		if (frequency == SBC_FREQUENCY_44100)
			config.sampleRate = 44100;
		else if (frequency == SBC_FREQUENCY_48000)
			config.sampleRate = 48000;
		else {
			error = AVDTP_ERROR_NOT_SUPPORTED_COMMAND;
			return false;
		}
		if (mode != SBC_MODE_MONO && mode != SBC_MODE_DUAL_CHANNEL
			&& mode != SBC_MODE_STEREO && mode != SBC_MODE_JOINT_STEREO) {
			error = AVDTP_ERROR_NOT_SUPPORTED_COMMAND;
			return false;
		}
		config.channelMode = mode;
		config.blocks = blocks == SBC_BLOCKS_4 ? 4 : blocks == SBC_BLOCKS_8 ? 8
			: blocks == SBC_BLOCKS_12 ? 12 : blocks == SBC_BLOCKS_16 ? 16 : 0;
		config.subbands = subbands == SBC_SUBBANDS_4 ? 4
			: subbands == SBC_SUBBANDS_8 ? 8 : 0;
		if (config.blocks == 0 || config.subbands == 0
			|| (allocation != SBC_ALLOCATION_SNR
				&& allocation != SBC_ALLOCATION_LOUDNESS)) {
			error = AVDTP_ERROR_NOT_SUPPORTED_COMMAND;
			return false;
		}
		config.allocation = allocation;
		const uint32 frameLimit = (mode == SBC_MODE_MONO
			|| mode == SBC_MODE_DUAL_CHANNEL ? 16 : 32) * config.subbands;
		config.minBitpool = max_c(value[4], (uint8)2);
		config.maxBitpool = value[5];
		uint32 bitpool = min_c((uint32)value[5], (uint32)fBitpoolLimit);
		bitpool = min_c(bitpool, frameLimit);
		if (bitpool < config.minBitpool) {
			error = AVDTP_ERROR_NOT_SUPPORTED_COMMAND;
			return false;
		}
		config.bitpool = bitpool;
		haveCodec = true;
	}
	if (!haveCodec) {
		category = AVDTP_MEDIA_CODEC;
		error = AVDTP_ERROR_NOT_SUPPORTED_COMMAND;
		return false;
	}

	BAutolock _(fLock);
	fConfiguration = config;
	fRemoteSEID = remote;
	fSinkDelayReporting = delayReporting;
	fState = CONFIGURED;
	return true;
}


status_t
A2dpSource::_Open()
{
	uint8 parameter = fRemoteSEID << 2;
	Response response;
	status_t status = _Command(AVDTP_OPEN, &parameter, 1, &response);
	if (status == B_NOT_ALLOWED && response.length >= 1) {
		return _Fail(status, "the sink refused to open the stream: %s",
			avdtp_error_string(response.data[0]));
	}
	if (status != B_OK)
		return _Fail(status, "AVDTP open: %s", strerror(status));

	// AVDTP 1.3 section 5.4.3: the initiator now opens the transport
	// channel, a second L2CAP channel to the same PSM.
	int media;
	status = open_l2cap(fAddress, L2CAP_PSM_AVDTP, media);
	if (status != B_OK)
		return _Fail(status, "AVDTP media channel: %s", strerror(status));

	uint16 mtu = 0;
	socklen_t size = sizeof(mtu);
	if (getsockopt(media, BLUETOOTH_PROTO_L2CAP, B_L2CAP_OUTGOING_MTU, &mtu,
			&size) != 0 || mtu < 48)
		mtu = L2CAP_MTU_DEFAULT;

	BAutolock _(fLock);
	fMediaSocket = media;
	fMediaMTU = mtu;
	fState = OPEN;
	LELog(LE_LOG_INFO, "a2dp", "stream open, media packets up to %u bytes",
		mtu);
	return B_OK;
}


status_t
A2dpSource::Start()
{
	if (fState == STREAMING)
		return B_OK;
	if (fState != OPEN)
		return B_NOT_ALLOWED;

	uint8 parameter = fRemoteSEID << 2;
	Response response;
	status_t status = _Command(AVDTP_START, &parameter, 1, &response);
	if (status == B_NOT_ALLOWED && response.length >= 2) {
		// The sink may have started the stream itself meanwhile.
		if (fState == STREAMING)
			return B_OK;
		return _Fail(status, "the sink refused to start: %s",
			avdtp_error_string(response.data[1]));
	}
	if (status != B_OK)
		return _Fail(status, "AVDTP start: %s", strerror(status));

	BAutolock _(fLock);
	if (fState == OPEN)
		fState = STREAMING;
	fTimestamp = 0;
	LELog(LE_LOG_INFO, "a2dp", "streaming");
	return B_OK;
}


status_t
A2dpSource::Suspend()
{
	if (fState != STREAMING)
		return fState == OPEN ? B_OK : B_NOT_ALLOWED;

	uint8 parameter = fRemoteSEID << 2;
	Response response;
	status_t status = _Command(AVDTP_SUSPEND, &parameter, 1, &response);
	if (status != B_OK && fState == STREAMING)
		return _Fail(status, "AVDTP suspend: %s", strerror(status));

	BAutolock _(fLock);
	if (fState == STREAMING)
		fState = OPEN;
	LELog(LE_LOG_INFO, "a2dp", "suspended");
	return B_OK;
}


void
A2dpSource::_CloseMedia()
{
	int media;
	{
		BAutolock _(fLock);
		media = fMediaSocket;
		fMediaSocket = -1;
		fMediaMTU = 0;
	}
	if (media >= 0)
		close(media);
}


void
A2dpSource::Disconnect()
{
	if (fSignalSocket >= 0 && fState != IDLE && fRemoteSEID != 0) {
		uint8 parameter = fRemoteSEID << 2;
		Response response;
		_Command(AVDTP_CLOSE, &parameter, 1, &response, 3000000);
	}
	{
		BAutolock _(fLock);
		fState = IDLE;
	}
	_CloseMedia();

	fQuit = true;
	if (fReader >= 0) {
		status_t result;
		wait_for_thread(fReader, &result);
		fReader = -1;
	}
	if (fSignalSocket >= 0) {
		close(fSignalSocket);
		fSignalSocket = -1;
	}
	fRemoteSEID = 0;
}


uint32
A2dpSource::MaxFramesPerPacket() const
{
	const size_t frameSize = fConfiguration.FrameSize();
	const size_t packetSize = min_c(fMediaMTU, (size_t)kMaxMediaPacket);
	if (packetSize <= 13 || frameSize == 0)
		return 0;
	// RTP header and the one octet SBC payload header; the frame count is
	// four bits.
	return min_c((packetSize - 13) / frameSize, (size_t)15);
}


status_t
A2dpSource::SendFrames(const uint8* frames, uint32 count)
{
	if (count == 0 || count > MaxFramesPerPacket())
		return B_BAD_VALUE;

	const size_t frameSize = fConfiguration.FrameSize();
	uint8 packet[kMaxMediaPacket];
	if (13 + count * frameSize > sizeof(packet))
		return B_BAD_VALUE;

	// RTP (RFC 3550): version 2, no padding, extension or CSRCs; a dynamic
	// payload type; the timestamp counts samples.
	packet[0] = 0x80;
	packet[1] = 96;
	packet[2] = fSequence >> 8;
	packet[3] = fSequence & 0xff;
	packet[4] = fTimestamp >> 24;
	packet[5] = (fTimestamp >> 16) & 0xff;
	packet[6] = (fTimestamp >> 8) & 0xff;
	packet[7] = fTimestamp & 0xff;
	packet[8] = 0;
	packet[9] = 0;
	packet[10] = 0;
	packet[11] = 1;
		// SSRC
	packet[12] = count;
		// not fragmented; the number of frames
	memcpy(packet + 13, frames, count * frameSize);

	status_t status = SendMediaPacket(packet, 13 + count * frameSize);
	if (status != B_OK)
		return status;

	fSequence++;
	fTimestamp += count * fConfiguration.FrameSamples();
	return B_OK;
}


status_t
A2dpSource::SendMediaPacket(const void* data, size_t size)
{
	int media = fMediaSocket;
	if (media < 0 || fState != STREAMING)
		return B_NOT_ALLOWED;
	if (size > fMediaMTU)
		return B_BAD_VALUE;
	if (send(media, data, size, 0) < 0)
		return errno;
	return B_OK;
}


} // namespace Bluetooth
