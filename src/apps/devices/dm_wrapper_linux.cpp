/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * The device manager interface of dm_wrapper.h on Linux. air/OS read the
 * device tree from the kernel's device manager through a generic syscall;
 * here it is read from sysfs through libudev and presented as the same tree
 * of nodes with the same attribute names, so the rest of Devices (DevicePCI,
 * DeviceUSB, DeviceACPI, DeviceSCSI) works unchanged:
 *
 *	Devices Root
 *	  ACPI                    /sys/devices/LNXSYSTM:00 and its ACPI nodes
 *	  PCI                     one per PCI host bridge (/sys/devices/pciDDDD:BB)
 *	    <pci device>          bridges keep their devices below them
 *	      USB                 one per USB root hub
 *	        <usb device>      the root hub, then hubs and devices
 *	      <scsi device>       disks and optical drives behind a controller
 *	  <other devices>         USB or SCSI devices with no PCI parent
 *
 * A node's "device paths" are the /dev nodes udev knows for the node and
 * for the devices below it that are not listed themselves (block devices,
 * input event nodes, DRM cards, sound devices, ...). "Driver used" is the
 * kernel driver bound to the device.
 *
 * The tree is built in init_dm_wrapper() and freed in uninit_dm_wrapper().
 * Children are sorted by their sysfs path, so a node keeps its cookie from
 * one scan to the next while the hardware does not change.
 */


#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <libudev.h>

#include <SupportDefs.h>
#include <TypeConstants.h>

#include <drivers/bus/USB.h>

extern "C" {
#include "dm_wrapper.h"
}


namespace {

struct NodeAttribute {
	std::string	name;
	type_code	type;
	uint64		number;
	std::string	string;
};

struct Node {
	std::string					syspath;
	std::vector<NodeAttribute>	attributes;
	std::string					driver;
	std::vector<std::string>	devicePaths;
	int32						parent;
	std::vector<int32>			children;
};

std::vector<Node> sNodes;
	// cookie = index + 1, the root is cookie 1; 0 is never a node


void
AddString(Node& node, const char* name, const std::string& value)
{
	NodeAttribute attribute;
	attribute.name = name;
	attribute.type = B_STRING_TYPE;
	attribute.number = 0;
	attribute.string = value;
	node.attributes.push_back(attribute);
}


void
AddNumber(Node& node, const char* name, type_code type, uint64 value)
{
	NodeAttribute attribute;
	attribute.name = name;
	attribute.type = type;
	attribute.number = value;
	node.attributes.push_back(attribute);
}


std::string
SysAttr(struct udev_device* device, const char* name)
{
	const char* value = udev_device_get_sysattr_value(device, name);
	if (value == NULL)
		return std::string();

	std::string result(value);
	while (!result.empty() && isspace((unsigned char)result[result.size() - 1]))
		result.erase(result.size() - 1);
	return result;
}


uint64
SysAttrNumber(struct udev_device* device, const char* name, int base)
{
	std::string value = SysAttr(device, name);
	if (value.empty())
		return 0;
	return strtoull(value.c_str(), NULL, base);
}


std::string
ParentPath(const std::string& path)
{
	std::string::size_type slash = path.rfind('/');
	if (slash == std::string::npos || slash == 0)
		return std::string();
	return path.substr(0, slash);
}


int32
NewNode(const std::string& syspath)
{
	Node node;
	node.syspath = syspath;
	node.parent = -1;
	sNodes.push_back(node);
	return (int32)sNodes.size() - 1;
}


// What kind of node a udev device becomes, if it is listed at all.
enum NodeKind {
	KIND_NONE,
	KIND_ACPI_ROOT,
	KIND_ACPI,
	KIND_PCI_HOST,
	KIND_PCI,
	KIND_USB,
	KIND_SCSI
};


NodeKind
KindOf(struct udev_device* device)
{
	const char* subsystem = udev_device_get_subsystem(device);
	const char* devtype = udev_device_get_devtype(device);
	const char* sysname = udev_device_get_sysname(device);
	if (subsystem == NULL)
		return KIND_NONE;

	if (strcmp(subsystem, "acpi") == 0) {
		if (sysname != NULL && strncmp(sysname, "LNXSYSTM:", 9) == 0)
			return KIND_ACPI_ROOT;
		return KIND_ACPI;
	}
	if (strcmp(subsystem, "pci") == 0)
		return KIND_PCI;
	if (strcmp(subsystem, "usb") == 0 && devtype != NULL
		&& strcmp(devtype, "usb_device") == 0)
		return KIND_USB;
	if (strcmp(subsystem, "scsi") == 0 && devtype != NULL
		&& strcmp(devtype, "scsi_device") == 0)
		return KIND_SCSI;
	return KIND_NONE;
}


void
FillAttributes(Node& node, NodeKind kind, struct udev_device* device)
{
	switch (kind) {
		case KIND_ACPI_ROOT:
			AddString(node, B_DEVICE_PRETTY_NAME, "ACPI");
			break;

		case KIND_ACPI:
		{
			AddString(node, B_DEVICE_BUS, "acpi");
			std::string path = SysAttr(device, "path");
			if (!path.empty())
				AddString(node, "acpi/path", path);
			std::string hid = SysAttr(device, "hid");
			if (!hid.empty())
				AddString(node, "acpi/hid", hid);
			// "acpi:HID:CID1:CID2:" - the first compatible ID
			std::string modalias = SysAttr(device, "modalias");
			if (modalias.compare(0, 5, "acpi:") == 0) {
				std::string::size_type first = modalias.find(':', 5);
				if (first != std::string::npos) {
					std::string::size_type second = modalias.find(':', first + 1);
					std::string cid = modalias.substr(first + 1,
						second == std::string::npos
							? std::string::npos : second - first - 1);
					if (!cid.empty())
						AddString(node, "acpi/cid", cid);
				}
			}
			std::string uid = SysAttr(device, "uid");
			if (!uid.empty())
				AddString(node, "acpi/uid", uid);
			break;
		}

		case KIND_PCI_HOST:
			AddString(node, B_DEVICE_PRETTY_NAME, "PCI");
			break;

		case KIND_PCI:
		{
			AddString(node, B_DEVICE_BUS, "pci");
			uint64 classCode = SysAttrNumber(device, "class", 16);
			AddNumber(node, B_DEVICE_TYPE, B_UINT16_TYPE,
				(classCode >> 16) & 0xff);
			AddNumber(node, B_DEVICE_SUB_TYPE, B_UINT16_TYPE,
				(classCode >> 8) & 0xff);
			AddNumber(node, B_DEVICE_INTERFACE, B_UINT16_TYPE,
				classCode & 0xff);
			AddNumber(node, B_DEVICE_VENDOR_ID, B_UINT16_TYPE,
				SysAttrNumber(device, "vendor", 16));
			AddNumber(node, B_DEVICE_ID, B_UINT16_TYPE,
				SysAttrNumber(device, "device", 16));
			AddNumber(node, "pci/subsystem_vendor", B_UINT16_TYPE,
				SysAttrNumber(device, "subsystem_vendor", 16));
			AddNumber(node, "pci/subsystem_id", B_UINT16_TYPE,
				SysAttrNumber(device, "subsystem_device", 16));
			AddNumber(node, "pci/revision", B_UINT8_TYPE,
				SysAttrNumber(device, "revision", 16));

			// "DDDD:BB:DD.F"
			unsigned int domain, bus, slot, function;
			const char* sysname = udev_device_get_sysname(device);
			if (sysname != NULL && sscanf(sysname, "%x:%x:%x.%x", &domain,
					&bus, &slot, &function) == 4) {
				AddNumber(node, "pci/domain", B_UINT16_TYPE, domain);
				AddNumber(node, "pci/bus", B_UINT8_TYPE, bus);
				AddNumber(node, "pci/device", B_UINT8_TYPE, slot);
				AddNumber(node, "pci/function", B_UINT8_TYPE, function);
			}
			std::string irq = SysAttr(device, "irq");
			if (!irq.empty() && irq != "0")
				AddString(node, "pci/interrupt", irq);
			break;
		}

		case KIND_USB:
			AddString(node, B_DEVICE_BUS, "usb");
			AddNumber(node, USB_DEVICE_CLASS, B_UINT8_TYPE,
				SysAttrNumber(device, "bDeviceClass", 16));
			AddNumber(node, USB_DEVICE_SUBCLASS, B_UINT8_TYPE,
				SysAttrNumber(device, "bDeviceSubClass", 16));
			AddNumber(node, USB_DEVICE_PROTOCOL, B_UINT8_TYPE,
				SysAttrNumber(device, "bDeviceProtocol", 16));
			AddNumber(node, B_DEVICE_VENDOR_ID, B_UINT16_TYPE,
				SysAttrNumber(device, "idVendor", 16));
			AddNumber(node, B_DEVICE_ID, B_UINT16_TYPE,
				SysAttrNumber(device, "idProduct", 16));
			{
				std::string speed = SysAttr(device, "speed");
				if (!speed.empty())
					AddString(node, "usb/speed", speed + " Mbit/s");
			}
			break;

		case KIND_SCSI:
		{
			AddString(node, B_DEVICE_BUS, "scsi");
			AddString(node, "scsi/vendor", SysAttr(device, "vendor"));
			AddString(node, "scsi/product", SysAttr(device, "model"));
			AddString(node, "scsi/revision", SysAttr(device, "rev"));
			// DeviceSCSI names the standard peripheral types 0x00-0x10 and
			// calls everything above "Other" (0x11).
			uint64 type = SysAttrNumber(device, "type", 10);
			if (type > 0x11)
				type = 0x11;
			AddNumber(node, "scsi/type", B_UINT8_TYPE, type);
			break;
		}

		case KIND_NONE:
			break;
	}

	const char* driver = udev_device_get_driver(device);
	if (driver != NULL)
		node.driver = driver;

	const char* devnode = udev_device_get_devnode(device);
	if (devnode != NULL)
		node.devicePaths.push_back(devnode);
}


int32
FindListed(const std::map<std::string, int32>& listed, std::string path)
{
	while (!path.empty()) {
		std::map<std::string, int32>::const_iterator found = listed.find(path);
		if (found != listed.end())
			return found->second;
		path = ParentPath(path);
	}
	return -1;
}


bool
CompareChildren(int32 a, int32 b)
{
	return sNodes[a].syspath < sNodes[b].syspath;
}


status_t
BuildTree()
{
	sNodes.clear();

	struct udev* udev = udev_new();
	if (udev == NULL)
		return errno != 0 ? -errno : B_ERROR;

	struct udev_enumerate* enumerate = udev_enumerate_new(udev);
	if (enumerate == NULL) {
		udev_unref(udev);
		return B_NO_MEMORY;
	}
	udev_enumerate_scan_devices(enumerate);

	// the root node, cookie 1
	int32 root = NewNode("/sys/devices");
	AddString(sNodes[root], B_DEVICE_PRETTY_NAME, "Devices Root");

	// sysfs path -> node index, for the parent lookup below
	std::map<std::string, int32> listed;
	// /dev nodes of devices that are not listed themselves
	std::vector<std::pair<std::string, std::string> > unlistedPaths;
	// USB root hubs, which get a "USB" bus node above them
	std::vector<int32> rootHubs;

	struct udev_list_entry* entry;
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
		const char* syspath = udev_list_entry_get_name(entry);
		struct udev_device* device
			= udev_device_new_from_syspath(udev, syspath);
		if (device == NULL)
			continue;

		NodeKind kind = KindOf(device);
		if (kind == KIND_NONE) {
			const char* devnode = udev_device_get_devnode(device);
			if (devnode != NULL)
				unlistedPaths.push_back(std::make_pair(syspath, devnode));
			udev_device_unref(device);
			continue;
		}

		if (kind == KIND_PCI) {
			// the host bridge directory (/sys/devices/pciDDDD:BB) is not a
			// device of its own; it becomes the "PCI" bus node.
			std::string path(syspath);
			std::string::size_type start = strlen("/sys/devices/");
			std::string::size_type end = path.find('/', start);
			if (path.compare(0, start, "/sys/devices/") == 0
				&& end != std::string::npos
				&& path.compare(start, 3, "pci") == 0) {
				std::string host = path.substr(0, end);
				if (listed.find(host) == listed.end()) {
					int32 index = NewNode(host);
					FillAttributes(sNodes[index], KIND_PCI_HOST, device);
					sNodes[index].driver.clear();
					sNodes[index].devicePaths.clear();
					listed[host] = index;
				}
			}
		}

		int32 index = NewNode(syspath);
		FillAttributes(sNodes[index], kind, device);
		listed[syspath] = index;

		if (kind == KIND_USB) {
			struct udev_device* parent
				= udev_device_get_parent_with_subsystem_devtype(device, "usb",
					"usb_device");
			if (parent == NULL)
				rootHubs.push_back(index);
		}

		udev_device_unref(device);
	}

	udev_enumerate_unref(enumerate);
	udev_unref(udev);

	// A "USB" bus node between each root hub and its host controller.
	for (size_t i = 0; i < rootHubs.size(); i++) {
		std::string hubPath = sNodes[rootHubs[i]].syspath;
		int32 bus = NewNode(hubPath + "/.usb-bus");
		AddString(sNodes[bus], B_DEVICE_PRETTY_NAME, "USB");
		sNodes[bus].parent = FindListed(listed, ParentPath(hubPath));
		sNodes[rootHubs[i]].parent = bus;
	}

	// Parents: the nearest listed ancestor in sysfs, else the root.
	for (int32 i = 1; i < (int32)sNodes.size(); i++) {
		Node& node = sNodes[i];
		if (node.syspath.find("/.usb-bus") != std::string::npos) {
			if (node.parent < 0)
				node.parent = root;
			continue;
		}
		if (node.parent >= 0)
			continue;
		int32 parent = FindListed(listed, ParentPath(node.syspath));
		node.parent = parent >= 0 && parent != i ? parent : root;
	}

	// Device paths of unlisted devices go to their nearest listed ancestor.
	for (size_t i = 0; i < unlistedPaths.size(); i++) {
		int32 owner = FindListed(listed, unlistedPaths[i].first);
		if (owner >= 0)
			sNodes[owner].devicePaths.push_back(unlistedPaths[i].second);
	}

	for (int32 i = 1; i < (int32)sNodes.size(); i++)
		sNodes[sNodes[i].parent].children.push_back(i);
	for (size_t i = 0; i < sNodes.size(); i++) {
		std::sort(sNodes[i].children.begin(), sNodes[i].children.end(),
			CompareChildren);
		std::sort(sNodes[i].devicePaths.begin(), sNodes[i].devicePaths.end());
	}

	for (size_t i = 0; i < sNodes.size(); i++) {
		Node& node = sNodes[i];
		if (node.devicePaths.empty())
			continue;
		std::string paths;
		for (size_t j = 0; j < node.devicePaths.size(); j++) {
			if (j > 0)
				paths += ", ";
			paths += node.devicePaths[j];
		}
		AddString(node, B_DEVICE_PUBLISHED_PATH, paths);
	}

	return B_OK;
}


Node*
NodeFor(device_node_cookie cookie)
{
	if (cookie == 0 || cookie > sNodes.size())
		return NULL;
	return &sNodes[cookie - 1];
}


void
CopyString(char* target, size_t size, const std::string& source)
{
	strncpy(target, source.c_str(), size - 1);
	target[size - 1] = '\0';
}

}	// namespace


status_t
init_dm_wrapper(void)
{
	return BuildTree();
}


status_t
uninit_dm_wrapper(void)
{
	sNodes.clear();
	return B_OK;
}


status_t
get_root(device_node_cookie* cookie)
{
	if (sNodes.empty())
		return B_NO_INIT;
	*cookie = 1;
	return B_OK;
}


status_t
get_child(device_node_cookie* cookie)
{
	Node* node = NodeFor(*cookie);
	if (node == NULL)
		return B_BAD_VALUE;
	if (node->children.empty())
		return B_ENTRY_NOT_FOUND;
	*cookie = node->children[0] + 1;
	return B_OK;
}


status_t
get_next_child(device_node_cookie* cookie)
{
	Node* node = NodeFor(*cookie);
	if (node == NULL || node->parent < 0)
		return B_BAD_VALUE;

	const std::vector<int32>& siblings = sNodes[node->parent].children;
	int32 index = (int32)(*cookie - 1);
	for (size_t i = 0; i + 1 < siblings.size(); i++) {
		if (siblings[i] == index) {
			*cookie = siblings[i + 1] + 1;
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


status_t
dm_get_next_attr(struct device_attr_info* attr)
{
	Node* node = NodeFor(attr->node_cookie);
	if (node == NULL)
		return B_BAD_VALUE;
	if (attr->cookie >= node->attributes.size())
		return B_ENTRY_NOT_FOUND;

	const NodeAttribute& attribute = node->attributes[attr->cookie++];
	CopyString(attr->name, sizeof(attr->name), attribute.name);
	attr->type = attribute.type;
	switch (attribute.type) {
		case B_UINT8_TYPE:
			attr->value.ui8 = (uint8)attribute.number;
			break;
		case B_UINT16_TYPE:
			attr->value.ui16 = (uint16)attribute.number;
			break;
		case B_UINT32_TYPE:
			attr->value.ui32 = (uint32)attribute.number;
			break;
		case B_UINT64_TYPE:
			attr->value.ui64 = attribute.number;
			break;
		default:
			CopyString(attr->value.string, sizeof(attr->value.string),
				attribute.string);
			break;
	}
	return B_OK;
}


status_t
dm_get_driver_path(struct device_attr_info* attr)
{
	attr->value.string[0] = '\0';
	Node* node = NodeFor(attr->node_cookie);
	if (node == NULL)
		return B_BAD_VALUE;
	CopyString(attr->value.string, sizeof(attr->value.string), node->driver);
	return B_OK;
}
