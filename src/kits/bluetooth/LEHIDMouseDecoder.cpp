/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include <LEHIDMouseDecoder.h>

#include <Errors.h>


// TODO(linux backend): air/OS decodes HID-over-GATT reports itself with the
// HID parser of the kernel's hid_shared module, which is gone with the Haiku
// kernel. On Linux BlueZ's HID-over-GATT profile hands LE mice, keyboards
// and game pads to the kernel through uhid, and they reach input_server as
// evdev devices. A userland decoder is only needed again for a device BlueZ
// does not claim; then it should use a userland HID parser.


namespace Bluetooth {


LEHIDMouseDecoder::LEHIDMouseDecoder()
	:
	fParser(NULL)
{
}


LEHIDMouseDecoder::~LEHIDMouseDecoder()
{
}


status_t
LEHIDMouseDecoder::Init(const uint8* reportMap, size_t length)
{
	return B_NOT_SUPPORTED;
}


bool
LEHIDMouseDecoder::SupportsReport(uint8 reportID) const
{
	return false;
}


status_t
LEHIDMouseDecoder::Decode(uint8 reportID, const uint8* value, size_t length,
	LEMouseReport& output)
{
	return B_NOT_SUPPORTED;
}


bool
LEHIDReportMapHasGamepad(const uint8* reportMap, size_t length)
{
	return false;
}


} // namespace Bluetooth
