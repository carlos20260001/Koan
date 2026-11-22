/*
 * Copyright 2001-2010 Haiku Inc. All rights reserved.
 * Copyright 2026, Koan contributors.
 * Distributed under the terms of the MIT License.
 *
 * Originally written based on Parallel port addon by Michael Pfeiffer,
 * changes by Andreas Benzler, Philippe Houdoin
 * Rewritten to use the USBKit by Ithamar R. Adema.
 *   (Using code from usb_printer.cpp by Michael Lotz)
 * On Linux, the kernel's usblp driver owns USB printer interfaces, so the
 * transport writes to its /dev/usb/lp* nodes instead of claiming the
 * interface through the USB kit.
 *
 * Authors:
 *		Ithamar R. Adema, <ithamar.adema@team-embedded.nl>
 *		Michael Pfeiffer,
 *		Andreas Benzler,
 *		Philippe Houdoin,
 */

#include "PrintTransportAddOn.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <new>

#include <Directory.h>
#include <Entry.h>
#include <String.h>


// usblp's IEEE 1284 device ID request (linux/drivers/usb/class/usblp.c):
// the reply starts with a big-endian 16-bit length that includes itself.
#define USBLP_IOCNR_GET_DEVICE_ID	1
#define USBLP_GET_DEVICE_ID(length) \
	_IOC(_IOC_READ, 'P', USBLP_IOCNR_GET_DEVICE_ID, length)

static const char* kUSBPrinterDirectory = "/dev/usb";


class USBTransport : public BDataIO {
public:
	USBTransport(BDirectory *printer, BMessage *msg);
	~USBTransport();

	status_t InitCheck() { return fFile >= 0 ? B_OK : B_ERROR; };

	ssize_t Read(void *buffer, size_t size);
	ssize_t Write(const void *buffer, size_t size);

private:
	int fFile;
};


static bool
is_printer_node(const char* name)
{
	if (strncmp(name, "lp", 2) != 0 || name[2] == '\0')
		return false;
	for (const char* c = name + 2; *c != '\0'; c++) {
		if (!isdigit((unsigned char)*c))
			return false;
	}
	return true;
}


// Returns the value of a key ("MFG", "MDL", ...) of an IEEE 1284 device ID.
static BString
device_id_field(const char* deviceID, const char* key, const char* longKey)
{
	BString id(deviceID);
	const char* keys[] = { key, longKey };
	for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
		BString pattern(keys[k]);
		pattern << ":";
		int32 start = 0;
		while ((start = id.FindFirst(pattern, start)) >= 0) {
			if (start == 0 || id[start - 1] == ';')
				break;
			start++;
		}
		if (start < 0)
			continue;
		start += pattern.Length();
		int32 end = id.FindFirst(';', start);
		if (end < 0)
			end = id.Length();
		BString value;
		id.CopyInto(value, start, end - start);
		value.Trim();
		return value;
	}
	return BString();
}


static BString
printer_name(const char* path)
{
	BString name;
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return name;

	unsigned char buffer[1024];
	if (ioctl(fd, USBLP_GET_DEVICE_ID(sizeof(buffer)), buffer) >= 0) {
		size_t length = (buffer[0] << 8) | buffer[1];
		if (length > sizeof(buffer) - 1)
			length = sizeof(buffer) - 1;
		if (length >= 2) {
			buffer[length] = '\0';
			const char* deviceID = (const char*)buffer + 2;
			BString manufacturer = device_id_field(deviceID, "MFG",
				"MANUFACTURER");
			BString model = device_id_field(deviceID, "MDL", "MODEL");
			name = manufacturer;
			if (model.Length() > 0) {
				if (name.Length() > 0)
					name << " ";
				name << model;
			}
		}
	}
	close(fd);
	return name;
}


// Implementation of transport add-on interface

BDataIO *
instantiate_transport(BDirectory *printer, BMessage *msg)
{
	USBTransport *transport = new(std::nothrow) USBTransport(printer, msg);
	if (transport != NULL && transport->InitCheck() == B_OK)
		return transport;

	delete transport;
	return NULL;
}


// List detected printers
status_t
list_transport_ports(BMessage *msg)
{
	BDirectory dir(kUSBPrinterDirectory);
	status_t status = dir.InitCheck();
	if (status != B_OK)
		return status;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK || !is_printer_node(name))
			continue;

		BString path(kUSBPrinterDirectory);
		path << "/" << name;
		BString portName = printer_name(path.String());
		if (portName.Length() > 0)
			portName << " (" << name << ")";

		msg->AddString("port_id", name);
		msg->AddString("port_name", portName);
	}

	return B_OK;
}


// Implementation of USBTransport
USBTransport::USBTransport(BDirectory *printer, BMessage *msg)
	: fFile(-1)
{
	BString key;

	if (printer->ReadAttrString("transport_address", &key) != B_OK
		|| !is_printer_node(key.String()))
		return;

	BString path(kUSBPrinterDirectory);
	path << "/" << key;

	bool bidirectional = true;
	fFile = open(path.String(), O_RDWR | O_CLOEXEC);
	if (fFile < 0) {
		bidirectional = false;
		fFile = open(path.String(), O_WRONLY | O_CLOEXEC);
	}
	if (fFile < 0)
		return;

	// If caller doesn't care...
	if (msg == NULL)
		return;

	// Fill up the message
	msg->what = 'okok';
	msg->AddBool("bidirectional", bidirectional);
	msg->AddString("_usb/DeviceName", path.String());
}


USBTransport::~USBTransport()
{
	if (fFile >= 0)
		close(fFile);
}


ssize_t
USBTransport::Read(void *buffer, size_t size)
{
	if (fFile < 0)
		return B_ERROR;
	return read(fFile, buffer, size);
}


ssize_t
USBTransport::Write(const void *buffer, size_t size)
{
	if (fFile < 0)
		return B_ERROR;
	return write(fFile, buffer, size);
}
