/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include <LEAttributeClient.h>

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <Errors.h>
#include <LELog.h>
#include <bluetooth/L2CAP/btL2CAP.h>


namespace Bluetooth {

#define LOG(level, format, args...) LELog(level, "att", format, ##args)

static const uint8 kATTErrorResponse = 0x01;
static const uint8 kATTExchangeMTURequest = 0x02;
static const uint8 kATTExchangeMTUResponse = 0x03;
static const uint8 kATTHandleValueNotification = 0x1b;
static const uint8 kATTHandleValueIndication = 0x1d;
static const uint8 kATTCommandFlag = 0x40;
static const uint8 kATTAttributeNotFound = 0x0a;
static const uint8 kATTAttributeNotLong = 0x0b;
static const uint8 kATTRequestNotSupported = 0x06;
static const size_t kATTDefaultMTU = 23;
static const size_t kMaximumAttributeLength = 4096;
// Notifications that arrive while a request waits for its answer are kept
// for ReadNotification(). A mouse being moved sends one every few
// milliseconds, so this has to hold what arrives during a slow exchange; past
// it the oldest go, which for input reports is the right ones to lose.
static const size_t kMaximumQueuedNotifications = 512;


static uint16
Read16(const uint8* bytes)
{
	return bytes[0] | ((uint16)bytes[1] << 8);
}


static void
Write16(uint8* bytes, uint16 value)
{
	bytes[0] = value;
	bytes[1] = value >> 8;
}


LEAttributeClient::LEAttributeClient()
	:
	fSocket(-1),
	fLastATTError(0)
{
}


LEAttributeClient::~LEAttributeClient()
{
	Disconnect();
}


status_t
LEAttributeClient::Connect(const bdaddr_t& address)
{
	Disconnect();

	// TODO(linux backend): this opens the ATT fixed channel with Haiku's
	// L2CAP socket address. With BlueZ, GATT goes through
	// org.bluez.GattService1/GattCharacteristic1 (bluetoothd owns the ATT
	// channel of a connected device); Linux L2CAP sockets also use another
	// address layout (sockaddr_l2). Until a GATT client over BlueZBackend
	// exists, LE attribute access is not available.
	return B_NOT_SUPPORTED;

	fSocket = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
	if (fSocket < 0) {
		status_t error = errno;
		LOG(LE_LOG_ERROR, "no L2CAP socket: %s", strerror(error));
		return error;
	}
	timeval sendTimeout = { 10, 0 };
	if (setsockopt(fSocket, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout,
			sizeof(sendTimeout)) != 0) {
		status_t error = errno;
		Disconnect();
		return error;
	}

	sockaddr_l2cap peer = {};
	peer.l2cap_len = sizeof(peer);
	peer.l2cap_family = AF_BLUETOOTH;
	peer.l2cap_psm = B_L2CAP_LE_ATT_CID;
	peer.l2cap_bdaddr = address;
	if (connect(fSocket, (const sockaddr*)&peer, sizeof(peer)) != 0) {
		status_t error = errno;
		char text[18];
		LOG(LE_LOG_ERROR, "opening the ATT channel to %s failed: %s",
			LEAddressString(address.b, text), strerror(error));
		Disconnect();
		return error;
	}
	LOG(LE_LOG_DEBUG, "ATT channel open");
	return B_OK;
}


void
LEAttributeClient::Disconnect()
{
	if (fSocket >= 0)
		close(fSocket);
	fSocket = -1;
	fLastATTError = 0;
	fNotifications.clear();
}


void
LEAttributeClient::AdoptConnectedSocket(int descriptor)
{
	Disconnect();
	fSocket = descriptor;
}


status_t
LEAttributeClient::_Receive(std::vector<uint8>& response)
{
	if (fSocket < 0)
		return B_NO_INIT;
	pollfd descriptor = { fSocket, POLLIN, 0 };
	int ready;
	do {
		ready = poll(&descriptor, 1, 5000);
	} while (ready < 0 && errno == EINTR);
	if (ready == 0)
		return B_TIMED_OUT;
	if (ready < 0)
		return B_ERROR;

	uint8 packet[517];
	ssize_t length = recv(fSocket, packet, sizeof(packet), 0);
	if (length < 0) {
		status_t error = errno;
		LOG(LE_LOG_INFO, "ATT receive failed: %s", strerror(error));
		return error == ENOTCONN ? B_DEV_NOT_READY : error;
	}
	if (length == 0)
		return B_DEV_NOT_READY;
	if (length > (ssize_t)kATTDefaultMTU) {
		LOG(LE_LOG_ERROR, "ATT PDU of %zd bytes exceeds the 23-byte MTU",
			length);
		return B_BAD_DATA;
	}
	// The kernel confirms indications; treat them like notifications.
	if (packet[0] == kATTHandleValueIndication)
		packet[0] = kATTHandleValueNotification;
	LELogHex(LE_LOG_TRACE, "att", "<-", packet, length);
	response.assign(packet, packet + length);
	return B_OK;
}


status_t
LEAttributeClient::_Exchange(const uint8* request, size_t requestLength,
	uint8 expectedOpcode, std::vector<uint8>& response)
{
	if (fSocket < 0)
		return B_NO_INIT;
	if (requestLength == 0 || requestLength > kATTDefaultMTU)
		return B_BAD_VALUE;
	fLastATTError = 0;
	LELogHex(LE_LOG_TRACE, "att", "->", request, requestLength);
	if (send(fSocket, request, requestLength, 0) != (ssize_t)requestLength) {
		status_t error = errno;
		LOG(LE_LOG_ERROR, "ATT request %#x not sent: %s", request[0],
			strerror(error));
		return error;
	}
	for (;;) {
		status_t status = _Receive(response);
		if (status != B_OK)
			return status;
		if (_HandlePeerPDU(response))
			continue;
		if (response[0] != kATTHandleValueNotification)
			break;
		if (response.size() < 3)
			return B_BAD_DATA;
		if (fNotifications.size() >= kMaximumQueuedNotifications)
			fNotifications.pop_front();
		Notification notification = { Read16(&response[1]),
			std::vector<uint8>(response.begin() + 3, response.end()) };
		fNotifications.push_back(notification);
	}
	if (response[0] == kATTErrorResponse && response.size() == 5
		&& response[1] == request[0]) {
		fLastATTError = response[4];
		if (fLastATTError != kATTAttributeNotFound) {
			LOG(LE_LOG_INFO, "ATT request %#x for handle %#x failed with "
				"error %#x", request[0], Read16(&response[2]), fLastATTError);
		}
		return fLastATTError == kATTAttributeNotFound
			? B_ENTRY_NOT_FOUND : B_ERROR;
	}
	if (response[0] != expectedOpcode) {
		LOG(LE_LOG_ERROR, "ATT request %#x answered with opcode %#x",
			request[0], response[0]);
		return B_BAD_DATA;
	}
	return B_OK;
}


status_t
LEAttributeClient::DiscoverPrimaryServices(
	std::vector<LEPrimaryService>& services)
{
	services.clear();
	uint16 start = 1;
	while (start != 0) {
		uint8 request[7] = { 0x10 };
		Write16(request + 1, start);
		Write16(request + 3, 0xffff);
		Write16(request + 5, 0x2800);
		std::vector<uint8> response;
		status_t status = _Exchange(request, sizeof(request), 0x11, response);
		if (status == B_ENTRY_NOT_FOUND)
			return B_OK;
		if (status != B_OK)
			return status;
		if (response.size() < 2 || (response[1] != 6 && response[1] != 20)
			|| response.size() < 2 + (size_t)response[1]
			|| (response.size() - 2) % response[1] != 0)
			return B_BAD_DATA;
		uint16 last = start - 1;
		for (size_t offset = 2; offset < response.size(); offset += response[1]) {
			LEPrimaryService service = { Read16(&response[offset]),
				Read16(&response[offset + 2]), 0 };
			if (response[1] == 6)
				service.uuid16 = Read16(&response[offset + 4]);
			if (service.startHandle <= last
				|| service.endHandle < service.startHandle)
				return B_BAD_DATA;
			last = service.endHandle;
			services.push_back(service);
		}
		if (last == 0xffff)
			return B_OK;
		start = last + 1;
	}
	return B_OK;
}


status_t
LEAttributeClient::DiscoverCharacteristics(uint16 startHandle,
	uint16 endHandle, std::vector<LECharacteristic>& characteristics)
{
	characteristics.clear();
	if (startHandle == 0 || startHandle > endHandle)
		return B_BAD_VALUE;
	while (startHandle <= endHandle) {
		uint8 request[7] = { 0x08 };
		Write16(request + 1, startHandle);
		Write16(request + 3, endHandle);
		Write16(request + 5, 0x2803);
		std::vector<uint8> response;
		status_t status = _Exchange(request, sizeof(request), 0x09, response);
		if (status == B_ENTRY_NOT_FOUND)
			return B_OK;
		if (status != B_OK)
			return status;
		if (response.size() < 2 || (response[1] != 7 && response[1] != 21)
			|| response.size() < 2 + (size_t)response[1]
			|| (response.size() - 2) % response[1] != 0)
			return B_BAD_DATA;
		uint16 last = startHandle - 1;
		for (size_t offset = 2; offset < response.size(); offset += response[1]) {
			LECharacteristic characteristic = { Read16(&response[offset]),
				Read16(&response[offset + 3]), response[offset + 2], 0 };
			if (response[1] == 7)
				characteristic.uuid16 = Read16(&response[offset + 5]);
			if (characteristic.declarationHandle <= last
				|| characteristic.valueHandle <= characteristic.declarationHandle
				|| characteristic.valueHandle > endHandle)
				return B_BAD_DATA;
			last = characteristic.declarationHandle;
			characteristics.push_back(characteristic);
		}
		if (last == endHandle)
			return B_OK;
		startHandle = last + 1;
	}
	return B_OK;
}


status_t
LEAttributeClient::FindClientConfiguration(uint16 startHandle,
	uint16 endHandle, uint16& handle)
{
	return FindDescriptor(startHandle, endHandle, 0x2902, handle);
}


status_t
LEAttributeClient::FindDescriptor(uint16 startHandle, uint16 endHandle,
	uint16 uuid16, uint16& handle)
{
	handle = 0;
	if (startHandle == 0 || startHandle > endHandle || uuid16 == 0)
		return B_BAD_VALUE;
	while (startHandle <= endHandle) {
		uint8 request[5] = { 0x04 };
		Write16(request + 1, startHandle);
		Write16(request + 3, endHandle);
		std::vector<uint8> response;
		status_t status = _Exchange(request, sizeof(request), 0x05, response);
		if (status != B_OK)
			return status;
		if (response.size() < 2 || (response[1] != 1 && response[1] != 2))
			return B_BAD_DATA;
		size_t entryLength = response[1] == 1 ? 4 : 18;
		if (response.size() < 2 + entryLength
			|| (response.size() - 2) % entryLength != 0)
			return B_BAD_DATA;
		uint16 last = startHandle - 1;
		for (size_t offset = 2; offset < response.size(); offset += entryLength) {
			uint16 candidate = Read16(&response[offset]);
			if (candidate <= last || candidate > endHandle)
				return B_BAD_DATA;
			last = candidate;
			if (entryLength == 4 && Read16(&response[offset + 2]) == uuid16) {
				handle = candidate;
				return B_OK;
			}
		}
		if (last == endHandle)
			return B_ENTRY_NOT_FOUND;
		startHandle = last + 1;
	}
	return B_ENTRY_NOT_FOUND;
}


status_t
LEAttributeClient::ReadAttribute(uint16 handle, std::vector<uint8>& value)
{
	value.clear();
	if (handle == 0)
		return B_BAD_VALUE;
	for (;;) {
		uint8 request[5] = { value.empty() ? (uint8)0x0a : (uint8)0x0c };
		Write16(request + 1, handle);
		if (!value.empty())
			Write16(request + 3, value.size());
		std::vector<uint8> response;
		status_t status = _Exchange(request, value.empty() ? 3 : 5,
			value.empty() ? 0x0b : 0x0d, response);
		if (status != B_OK) {
			if (!value.empty() && (fLastATTError == kATTAttributeNotLong
				|| fLastATTError == 0x07))
				return B_OK;
			return status;
		}
		if (value.size() + response.size() - 1 > kMaximumAttributeLength)
			return B_BAD_DATA;
		value.insert(value.end(), response.begin() + 1, response.end());
		if (response.size() < kATTDefaultMTU)
			return B_OK;
	}
}


status_t
LEAttributeClient::WriteAttribute(uint16 handle, const uint8* value,
	size_t length)
{
	if (handle == 0 || (value == NULL && length != 0)
		|| length > kATTDefaultMTU - 3)
		return B_BAD_VALUE;
	uint8 request[kATTDefaultMTU] = { 0x12 };
	Write16(request + 1, handle);
	if (length > 0)
		memcpy(request + 3, value, length);
	std::vector<uint8> response;
	status_t status = _Exchange(request, length + 3, 0x13, response);
	return status == B_OK && response.size() != 1 ? B_BAD_DATA : status;
}


status_t
LEAttributeClient::ReadNotification(uint16& handle, std::vector<uint8>& value)
{
	handle = 0;
	value.clear();
	if (!fNotifications.empty()) {
		handle = fNotifications.front().handle;
		value.swap(fNotifications.front().value);
		fNotifications.pop_front();
		return B_OK;
	}
	std::vector<uint8> response;
	for (;;) {
		status_t status = _Receive(response);
		if (status != B_OK)
			return status;
		if (_HandlePeerPDU(response))
			continue;
		if (response[0] == kATTHandleValueNotification)
			break;
		// The late answer to a request that gave up waiting for it, most
		// likely; nothing is waiting for it any more.
		LOG(LE_LOG_DEBUG, "ignoring unexpected ATT PDU %#x (%zu bytes)",
			response[0], response.size());
	}
	if (response.size() < 3)
		return B_BAD_DATA;
	handle = Read16(&response[1]);
	value.assign(response.begin() + 3, response.end());
	return B_OK;
}


/*!	The peer is a GATT client too: a mouse may ask for the MTU or look for
	services on this side. Nothing is served here, but every request needs an
	answer, or the peer waits 30 seconds and then drops the link. Returns
	whether \a pdu was such a request or command, and so has been dealt with.
*/
bool
LEAttributeClient::_HandlePeerPDU(const std::vector<uint8>& pdu)
{
	const uint8 opcode = pdu[0];
	if ((opcode & kATTCommandFlag) != 0) {
		// a command (Write Command, Signed Write): no answer expected
		LOG(LE_LOG_DEBUG, "ignoring ATT command %#x from the peer", opcode);
		return true;
	}
	// Requests have even opcodes up to 0x20; 0x1e is the confirmation of an
	// indication, which this side never sends.
	if ((opcode & 1) != 0 || opcode > 0x20 || opcode == 0x1e)
		return false;

	uint8 answer[5];
	size_t answerLength;
	if (opcode == kATTExchangeMTURequest) {
		answer[0] = kATTExchangeMTUResponse;
		Write16(answer + 1, kATTDefaultMTU);
		answerLength = 3;
	} else {
		// Discovery finds nothing; anything else is not supported.
		bool discovery = opcode == 0x04 || opcode == 0x06 || opcode == 0x08
			|| opcode == 0x10;
		answer[0] = kATTErrorResponse;
		answer[1] = opcode;
		Write16(answer + 2, pdu.size() >= 3 ? Read16(&pdu[1]) : 0);
		answer[4] = discovery ? kATTAttributeNotFound : kATTRequestNotSupported;
		answerLength = 5;
	}
	LOG(LE_LOG_DEBUG, "answering ATT request %#x from the peer", opcode);
	LELogHex(LE_LOG_TRACE, "att", "->", answer, answerLength);
	if (send(fSocket, answer, answerLength, 0) != (ssize_t)answerLength) {
		LOG(LE_LOG_ERROR, "could not answer ATT request %#x: %s", opcode,
			strerror(errno));
	}
	return true;
}

} // namespace Bluetooth
