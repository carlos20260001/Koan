/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "HardwareCaps.h"

#include <Autolock.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CedarVideoEncoder.h"
#include "IrisVideoEncoder.h"
#include "MppVideoEncoder.h"
#include "NvencEncoder.h"
#include "VaapiEncoder.h"
#include "VaapiLibrary.h"


static const uint16 kAMDVendor = 0x1002;
static const uint16 kNVIDIAVendor = 0x10de;
static const uint16 kIntelVendor = 0x8086;


bool
vaapi_encoder_implements(video_codec codec)
{
	switch (codec) {
		case kVideoH264:
		case kVideoH265:
		case kVideoH265_10:
		case kVideoAV1:
		case kVideoAV1_10:
			return true;
		default:
			return false;
	}
}


bool
nvenc_encoder_implements(video_codec codec)
{
	return NvencEncoder::Implements(codec);
}


bool
iris_encoder_implements(video_codec codec)
{
	return IrisVideoEncoder::Implements(codec);
}


bool
cedar_encoder_implements(video_codec codec)
{
	return CedarVideoEncoder::Implements(codec);
}


// #pragma mark - GpuDevice


const GpuEncodeFormat*
GpuDevice::Format(video_codec codec) const
{
	for (size_t i = 0; i < encode.size(); i++) {
		if (encode[i].codec == codec)
			return &encode[i];
	}
	return NULL;
}


bool
GpuDevice::CanEncode(video_codec codec, int width, int height) const
{
	if (!ready)
		return false;
	const GpuEncodeFormat* format = Format(codec);
	if (format == NULL)
		return false;
	if (backend == kBackendVAAPI && !vaapi_encoder_implements(codec))
		return false;
	if (backend == kBackendNVENC && !nvenc_encoder_implements(codec))
		return false;
	if (backend == kBackendIris && !iris_encoder_implements(codec))
		return false;
	if (backend == kBackendCedar && !cedar_encoder_implements(codec))
		return false;
	if (backend == kBackendRockchip && !MppVideoEncoder::Implements(codec))
		return false;
	return (format->maxWidth == 0 || width <= format->maxWidth)
		&& (format->maxHeight == 0 || height <= format->maxHeight);
}


// #pragma mark - tables


struct NvidiaChip {
	uint16		first;
	uint16		last;
	const char*	chip;
	const char*	generation;
	int			nvencGeneration;	// 0: none
};


// PCI device ids by chip, as the ranges NVIDIA allocates them in
static const NvidiaChip kNvidiaChips[] = {
	{ 0x0fc0, 0x0fff, "GK107", "Kepler", 1 },
	{ 0x1000, 0x103f, "GK110", "Kepler", 1 },
	{ 0x1180, 0x11ff, "GK104/GK106", "Kepler", 1 },
	{ 0x1280, 0x12bf, "GK208", "Kepler", 1 },
	{ 0x1340, 0x137f, "GM108", "Maxwell", 0 },
	{ 0x1380, 0x13bf, "GM107", "Maxwell", 4 },
	{ 0x13c0, 0x13ff, "GM204", "Maxwell", 5 },
	{ 0x1400, 0x143f, "GM206", "Maxwell", 5 },
	{ 0x15f0, 0x15ff, "GP100", "Pascal", 6 },
	{ 0x17c0, 0x17ff, "GM200", "Maxwell", 5 },
	{ 0x1b00, 0x1b3f, "GP102", "Pascal", 6 },
	{ 0x1b80, 0x1bbf, "GP104", "Pascal", 6 },
	{ 0x1c00, 0x1c3f, "GP106", "Pascal", 6 },
	{ 0x1c80, 0x1cbf, "GP107", "Pascal", 6 },
	{ 0x1d00, 0x1d3f, "GP108", "Pascal", 0 },
	{ 0x1d80, 0x1dbf, "GV100", "Volta", 6 },
	{ 0x1e00, 0x1e3f, "TU102", "Turing", 7 },
	{ 0x1e80, 0x1ebf, "TU104", "Turing", 7 },
	{ 0x1f00, 0x1f7f, "TU106", "Turing", 7 },
	{ 0x1f80, 0x1fff, "TU117", "Turing", 6 },
	{ 0x2080, 0x20ff, "GA100", "Ampere", 0 },
	{ 0x2180, 0x21ff, "TU116", "Turing", 7 },
	{ 0x2200, 0x223f, "GA102", "Ampere", 7 },
	{ 0x2300, 0x233f, "GH100", "Hopper", 0 },
	{ 0x2400, 0x243f, "GA103", "Ampere", 7 },
	{ 0x2480, 0x24ff, "GA104", "Ampere", 7 },
	{ 0x2500, 0x257f, "GA106", "Ampere", 7 },
	{ 0x2580, 0x25ff, "GA107", "Ampere", 7 },
	{ 0x2680, 0x26ff, "AD102", "Ada Lovelace", 8 },
	{ 0x2700, 0x277f, "AD103", "Ada Lovelace", 8 },
	{ 0x2780, 0x27ff, "AD104", "Ada Lovelace", 8 },
	{ 0x2800, 0x287f, "AD106", "Ada Lovelace", 8 },
	{ 0x2880, 0x28ff, "AD107", "Ada Lovelace", 8 },
	{ 0x2b80, 0x2bff, "GB202", "Blackwell", 9 },
	{ 0x2c00, 0x2c7f, "GB203", "Blackwell", 9 },
	{ 0x2d00, 0x2d7f, "GB206", "Blackwell", 9 },
	{ 0x2d80, 0x2dff, "GB207", "Blackwell", 9 },
	{ 0x2f00, 0x2f7f, "GB205", "Blackwell", 9 },
};


static const NvidiaChip*
nvidia_chip(uint16 device)
{
	for (size_t i = 0; i < sizeof(kNvidiaChips) / sizeof(kNvidiaChips[0]);
			i++) {
		if (device >= kNvidiaChips[i].first && device <= kNvidiaChips[i].last)
			return &kNvidiaChips[i];
	}
	return NULL;
}


static void
add_format(GpuDevice& device, video_codec codec, int maxWidth, int maxHeight,
	bool bFrames)
{
	GpuEncodeFormat format;
	format.codec = codec;
	format.maxWidth = maxWidth;
	format.maxHeight = maxHeight;
	format.bFrames = bFrames;
	device.encode.push_back(format);
}


/*!	NVENC by generation, from NVIDIA's video encode support matrix: H.264
	from the first, H.265 from the second Maxwell, ten-bit H.265 from
	Pascal, AV1 from Ada Lovelace. The nvenc add-on codes B pictures for
	H.264 only (H.265's wait for its P pictures), whatever the engine has. */
static void
nvidia_formats(GpuDevice& device, int generation)
{
	if (generation <= 0)
		return;
	const bool bFrames = false;
	add_format(device, kVideoH264, 4096, 4096, true);
	if (generation >= 5) {
		int size = generation >= 6 ? 8192 : 4096;
		add_format(device, kVideoH265, size, size, bFrames);
	}
	if (generation >= 6)
		add_format(device, kVideoH265_10, 8192, 8192, bFrames);
	if (generation >= 8) {
		add_format(device, kVideoAV1, 8192, 8192, bFrames);
		add_format(device, kVideoAV1_10, 8192, 8192, bFrames);
	}
}


struct AmdFamily {
	uint16		first;
	uint16		last;
	const char*	generation;
	const char*	engine;
	int			level;	// 1 VCE: H.264, 2 + HEVC, 3 + HEVC 10, 4 + AV1
};


static const AmdFamily kAmdFamilies[] = {
	{ 0x6780, 0x679f, "Southern Islands", "VCE 1", 1 },
	{ 0x6640, 0x665f, "Sea Islands", "VCE 2", 1 },
	{ 0x67a0, 0x67bf, "Sea Islands", "VCE 2", 1 },
	{ 0x6920, 0x693f, "Volcanic Islands", "VCE 3", 1 },
	{ 0x7300, 0x730f, "Fiji", "VCE 3", 1 },
	{ 0x67c0, 0x67ff, "Polaris", "VCE 3.4 and UVD 6.3", 2 },
	{ 0x6980, 0x699f, "Polaris", "VCE 3.4 and UVD 6.3", 2 },
	{ 0x694c, 0x694f, "Vega M", "VCE 4", 2 },
	{ 0x6860, 0x687f, "Vega", "VCE 4", 2 },
	{ 0x66a0, 0x66af, "Vega 20", "VCE 4.1", 2 },
	{ 0x15d8, 0x15dd, "Raven", "VCN 1", 2 },
	{ 0x1636, 0x1638, "Renoir", "VCN 2", 3 },
	{ 0x164c, 0x164c, "Lucienne", "VCN 2", 3 },
	{ 0x163f, 0x163f, "Van Gogh", "VCN 3", 3 },
	{ 0x164d, 0x164d, "Rembrandt", "VCN 3", 3 },
	{ 0x1681, 0x1681, "Rembrandt", "VCN 3", 3 },
	{ 0x7310, 0x731f, "Navi 10", "VCN 2", 3 },
	{ 0x7340, 0x734f, "Navi 14", "VCN 2", 3 },
	{ 0x73a0, 0x73bf, "Navi 21", "VCN 3", 3 },
	{ 0x73c0, 0x73df, "Navi 22", "VCN 3", 3 },
	{ 0x73e0, 0x73ff, "Navi 23", "VCN 3", 3 },
	{ 0x7420, 0x743f, "Navi 24", "VCN 3", 3 },
	{ 0x7440, 0x745f, "Navi 31", "VCN 4", 4 },
	{ 0x7460, 0x747f, "Navi 32", "VCN 4", 4 },
	{ 0x7480, 0x749f, "Navi 33", "VCN 4", 4 },
	{ 0x15bf, 0x15bf, "Phoenix", "VCN 4", 4 },
	{ 0x15c8, 0x15c8, "Phoenix 2", "VCN 4", 4 },
	{ 0x150e, 0x150e, "Strix Point", "VCN 4", 4 },
	{ 0x1586, 0x1586, "Strix Halo", "VCN 4", 4 },
	{ 0x7550, 0x755f, "Navi 48", "VCN 5", 4 },
	{ 0x7590, 0x759f, "Navi 44", "VCN 5", 4 },
};


static const AmdFamily*
amd_family(uint16 device)
{
	for (size_t i = 0; i < sizeof(kAmdFamilies) / sizeof(kAmdFamilies[0]);
			i++) {
		if (device >= kAmdFamilies[i].first && device <= kAmdFamilies[i].last)
			return &kAmdFamilies[i];
	}
	return NULL;
}


static void
amd_formats(GpuDevice& device, int level)
{
	if (level >= 1)
		add_format(device, kVideoH264, 4096, 4096, level >= 4);
	if (level >= 2)
		add_format(device, kVideoH265, 4096, 4096, false);
	if (level >= 3)
		add_format(device, kVideoH265_10, 4096, 4096, false);
	if (level >= 4) {
		add_format(device, kVideoAV1, 8192, 4352, true);
		add_format(device, kVideoAV1_10, 8192, 4352, true);
	}
}


// #pragma mark - HardwareInventory


HardwareInventory::HardwareInventory()
	:
	fLock("hardware inventory"),
	fProbed(false)
{
}


HardwareInventory&
HardwareInventory::Default()
{
	static HardwareInventory sInventory;
	return sInventory;
}


static const char*
va_profile_name(VAProfile profile)
{
	switch (profile) {
		case VAProfileMPEG2Simple:
		case VAProfileMPEG2Main:
			return "MPEG-2";
		case VAProfileVC1Simple:
		case VAProfileVC1Main:
		case VAProfileVC1Advanced:
			return "VC-1";
		case VAProfileH264ConstrainedBaseline:
		case VAProfileH264Main:
		case VAProfileH264High:
			return "H.264";
		case VAProfileH264High10:
			return "H.264 10-bit";
		case VAProfileHEVCMain:
			return "H.265";
		case VAProfileHEVCMain10:
			return "H.265 10-bit";
		case VAProfileHEVCMain12:
			return "H.265 12-bit";
		case VAProfileVP8Version0_3:
			return "VP8";
		case VAProfileVP9Profile0:
			return "VP9";
		case VAProfileVP9Profile2:
			return "VP9 10-bit";
		case VAProfileAV1Profile0:
			return "AV1";
		case VAProfileJPEGBaseline:
			return "JPEG";
		default:
			return NULL;
	}
}


void
HardwareInventory::_ProbeVaapi()
{
	BString nodes[8];
	int count = vaapi_render_nodes(nodes, 8);
	for (int n = 0; n < count; n++) {
		VaapiDisplay display;
		BString error;
		GpuDevice device;
		device.id.SetToFormat("vaapi:%s", nodes[n].String());
		device.devicePath = nodes[n];
		device.backend = kBackendVAAPI;
		device.vendorID = 0;
		device.deviceID = 0;
		if (display.Open(nodes[n].String(), error) != B_OK) {
			// a render node without a VA driver: not a video device to us
			continue;
		}
		BString vendor = display.Vendor();
		device.name = vaapi_device_name(vendor.String());
		if (device.name.IsEmpty())
			device.name = nodes[n];
		device.vendor = vendor.IFindFirst("AMD") >= 0
				|| vendor.IFindFirst("Radeon") >= 0 ? kVendorAMD
			: vendor.IFindFirst("Intel") >= 0 ? kVendorIntel
			: vendor.IFindFirst("NVIDIA") >= 0 ? kVendorNVIDIA : kVendorOther;
		if (device.vendor == kVendorAMD) {
			device.vendorID = kAMDVendor;
			int32 start = vendor.FindFirst("(radeonsi, ");
			if (start >= 0) {
				BString chip(vendor.String() + start + 11);
				chip.Truncate(chip.FindFirst(','));
				device.generation = chip;
			}
		}
		device.ready = true;
		device.status = "Ready";

		const VaapiLibrary* va = display.Library();
		VADisplay handle = display.Display();
		int profileCount = va->MaxNumProfiles(handle);
		std::vector<VAProfile> profiles(profileCount);
		if (va->QueryConfigProfiles(handle, profiles.data(), &profileCount)
				!= VA_STATUS_SUCCESS)
			profileCount = 0;
		int entryMax = va->MaxNumEntrypoints(handle);
		std::vector<VAEntrypoint> entries(entryMax);

		for (int p = 0; p < profileCount; p++) {
			int entryCount = entryMax;
			if (va->QueryConfigEntrypoints(handle, profiles[p], entries.data(),
					&entryCount) != VA_STATUS_SUCCESS)
				continue;
			for (int e = 0; e < entryCount; e++) {
				const char* name = va_profile_name(profiles[p]);
				if (entries[e] == VAEntrypointVLD) {
					if (name == NULL)
						continue;
					bool known = false;
					for (size_t i = 0; i < device.decode.size(); i++)
						known |= device.decode[i] == name;
					if (!known)
						device.decode.push_back(BString(name));
					continue;
				}
				if (entries[e] != VAEntrypointEncSlice
					&& entries[e] != VAEntrypointEncSliceLP)
					continue;

				VAConfigAttrib attributes[] = {
					{ VAConfigAttribRTFormat, 0 },
					{ VAConfigAttribMaxPictureWidth, 0 },
					{ VAConfigAttribMaxPictureHeight, 0 },
					{ VAConfigAttribEncMaxRefFrames, 0 },
				};
				va->GetConfigAttributes(handle, profiles[p], entries[e],
					attributes, 4);
				int maxWidth = attributes[1].value == VA_ATTRIB_NOT_SUPPORTED
					? 0 : (int)attributes[1].value;
				int maxHeight = attributes[2].value == VA_ATTRIB_NOT_SUPPORTED
					? 0 : (int)attributes[2].value;
				bool bFrames = attributes[3].value != VA_ATTRIB_NOT_SUPPORTED
					&& (attributes[3].value >> 16) > 0;
				bool tenBit = attributes[0].value != VA_ATTRIB_NOT_SUPPORTED
					&& (attributes[0].value & VA_RT_FORMAT_YUV420_10) != 0;

				video_codec codecs[2] = { kVideoCodecCount, kVideoCodecCount };
				switch (profiles[p]) {
					case VAProfileH264ConstrainedBaseline:
					case VAProfileH264Main:
					case VAProfileH264High:
						codecs[0] = kVideoH264;
						break;
					case VAProfileHEVCMain:
						codecs[0] = kVideoH265;
						break;
					case VAProfileHEVCMain10:
						codecs[0] = kVideoH265_10;
						break;
					case VAProfileAV1Profile0:
						codecs[0] = kVideoAV1;
						if (tenBit)
							codecs[1] = kVideoAV1_10;
						break;
					case VAProfileVP9Profile0:
						codecs[0] = kVideoVP9;
						break;
					case VAProfileVP9Profile2:
						codecs[0] = kVideoVP9_10;
						break;
					case VAProfileVP8Version0_3:
						codecs[0] = kVideoVP8;
						break;
					case VAProfileMPEG2Main:
						codecs[0] = kVideoMPEG2;
						break;
					default:
						break;
				}
				for (int c = 0; c < 2; c++) {
					if (codecs[c] == kVideoCodecCount
						|| device.Format(codecs[c]) != NULL)
						continue;
					add_format(device, codecs[c], maxWidth, maxHeight,
						bFrames);
				}
			}
		}

		if (device.vendor == kVendorAMD) {
			bool vce = device.Format(kVideoH264) != NULL;
			bool av1 = device.Format(kVideoAV1) != NULL;
			if (av1)
				device.engine = "VCN 4 or later";
			else if (device.Format(kVideoH265_10) != NULL)
				device.engine = "VCN";
			else if (vce)
				device.engine = "VCE";
		}
		fDevices.push_back(device);
	}
}


/*!	Reads one hexadecimal value from a sysfs file ("0x10de\n"). */
static bool
read_sysfs_hex(const char* directory, const char* name, uint32& _value)
{
	BString path;
	path.SetToFormat("%s/%s", directory, name);
	FILE* file = fopen(path.String(), "r");
	if (file == NULL)
		return false;
	unsigned int value = 0;
	bool ok = fscanf(file, "%x", &value) == 1;
	fclose(file);
	if (ok)
		_value = value;
	return ok;
}


/*!	The display controllers on the PCI bus. On Linux the kernel lists them
	in sysfs (/sys/bus/pci/devices/<address>/{class,vendor,device}), where
	air/OS's walked its device manager's tree.
*/
void
HardwareInventory::_ProbePci()
{
	static const char* kDevices = "/sys/bus/pci/devices";
	DIR* dir = opendir(kDevices);
	if (dir == NULL)
		return;

	std::vector<std::string> names;
	while (struct dirent* entry = readdir(dir)) {
		if (entry->d_name[0] != '.')
			names.push_back(entry->d_name);
	}
	closedir(dir);
	// the bus order, so that the list does not change between runs
	std::sort(names.begin(), names.end());

	for (size_t i = 0; i < names.size() && i < 4096; i++) {
		BString device;
		device.SetToFormat("%s/%s", kDevices, names[i].c_str());
		uint32 pciClass = 0, vendor = 0, deviceID = 0;
		if (!read_sysfs_hex(device.String(), "class", pciClass)
			|| !read_sysfs_hex(device.String(), "vendor", vendor)
			|| !read_sysfs_hex(device.String(), "device", deviceID))
			continue;
		// display controllers (base class 0x03)
		if ((pciClass >> 16) == 0x03)
			_AddTableDevice((uint16)vendor, (uint16)deviceID);
	}
}


static bool
exists(const char* path)
{
	struct stat st;
	return stat(path, &st) == 0;
}


void
HardwareInventory::_AddTableDevice(uint16 vendor, uint16 deviceID)
{
	if (vendor == kAMDVendor) {
		// a VA-API device already stands for an AMD card that has a driver
		for (size_t i = 0; i < fDevices.size(); i++) {
			if (fDevices[i].vendor == kVendorAMD
				&& fDevices[i].deviceID == 0) {
				fDevices[i].deviceID = deviceID;
				const AmdFamily* family = amd_family(deviceID);
				if (family != NULL) {
					fDevices[i].generation = family->generation;
					fDevices[i].engine = family->engine;
				}
				return;
			}
		}
	}

	GpuDevice device;
	device.vendorID = vendor;
	device.deviceID = deviceID;
	device.ready = false;
	device.id.SetToFormat("pci:%04x:%04x", vendor, deviceID);

	switch (vendor) {
		case kAMDVendor:
		{
			device.vendor = kVendorAMD;
			device.backend = kBackendVAAPI;
			const AmdFamily* family = amd_family(deviceID);
			device.name.SetToFormat("AMD Radeon [%04x]", deviceID);
			if (family != NULL) {
				device.generation = family->generation;
				device.engine = family->engine;
				device.name.SetToFormat("AMD Radeon (%s)", family->generation);
				amd_formats(device, family->level);
			}
			device.status = "Needs the amdgpu_drm driver and its VA-API "
				"user land.";
			break;
		}

		case kNVIDIAVendor:
		{
			device.vendor = kVendorNVIDIA;
			device.backend = kBackendNVENC;
			const NvidiaChip* chip = nvidia_chip(deviceID);
			if (chip != NULL) {
				device.generation = chip->generation;
				device.name.SetToFormat("NVIDIA %s (%s)", chip->generation,
					chip->chip);
				if (chip->nvencGeneration > 0) {
					device.engine.SetToFormat("NVENC, %s", chip->generation);
					nvidia_formats(device, chip->nvencGeneration);
				} else
					device.engine = "No video encoder";
			} else {
				device.name.SetToFormat("NVIDIA GPU [%04x]", deviceID);
				// newer than the table: assume the latest generation
				if (deviceID > 0x2f7f) {
					device.generation = "newer than Blackwell";
					nvidia_formats(device, 9);
				}
			}
			if (device.encode.empty())
				device.status = "This GPU has no video encoder.";
			else if (!exists("/dev/nvidiactl")
				&& getenv("MEDIACONVERTER_FAKE_NVIDIA") == NULL)
				device.status = "Needs the nvidia_rm driver.";
			else {
				BString why;
				device.ready = NvencEncoder::IsAvailable(&why);
				device.devicePath = "/dev/nvidiactl";
				device.status = device.ready ? BString("Ready") : why;
			}
			device.id.SetToFormat("nvenc:%04x", deviceID);
			break;
		}

		case kIntelVendor:
			device.vendor = kVendorIntel;
			device.backend = kBackendVAAPI;
			device.name.SetToFormat("Intel graphics [%04x]", deviceID);
			device.status = "air/OS has no VA-API driver for Intel graphics "
				"yet.";
			break;

		default:
			return;
	}
	fDevices.push_back(device);
}


/*!	The Qualcomm SC8280XP's video codec (Radxa Dragon Q8B), where the
	qcom_iris driver publishes it: H.264 High and HEVC Main up to 4096x2304,
	no B pictures.
*/
void
HardwareInventory::_ProbeIris()
{
	// TODO(linux backend): air/OS's qcom_iris driver interface; Linux
	// drives the Iris/Venus codec as a V4L2 memory-to-memory encoder
	// (/dev/videoN, driver "qcom-venus"/"iris"), which IrisDevice does not
	// speak yet, so no device is found here.
	if (!IrisVideoEncoder::IsAvailable())
		return;
	GpuDevice device;
	device.id = "iris:/dev/video/qcom_iris/0";
	device.name = "Qualcomm SC8280XP";
	device.vendor = kVendorOther;
	device.vendorID = 0;
	device.deviceID = 0;
	device.generation = "Snapdragon 8cx Gen 3";
	device.engine = "Iris (VPU 2.0)";
	device.backend = kBackendIris;
	device.devicePath = "/dev/video/qcom_iris/0";
	device.ready = true;
	device.encode.push_back(GpuEncodeFormat{ kVideoH264, 4096, 2304, false });
	device.encode.push_back(GpuEncodeFormat{ kVideoH265, 4096, 2304, false });
	device.decode.push_back("H.264");
	device.decode.push_back("H.265");
	fDevices.push_back(device);
}


/*!	The Radxa Cubie A7S's SoC: its video engine's encoder, through the
	sunxi_ve driver. */
void
HardwareInventory::_ProbeCedar()
{
	// TODO(linux backend): air/OS's sunxi_ve driver interface; mainline
	// Linux has no Cedar encoder driver (cedrus decodes only), so no device
	// is found here.
	if (!CedarVideoEncoder::IsAvailable())
		return;
	GpuDevice device;
	device.id = "cedar:/dev/misc/sunxi_ve";
	device.name = "Allwinner A733";
	device.vendor = kVendorOther;
	device.vendorID = 0;
	device.deviceID = 0;
	device.generation = "sun60iw2";
	device.engine = "Cedar video engine";
	device.backend = kBackendCedar;
	device.devicePath = "/dev/misc/sunxi_ve";
	device.ready = true;
	device.encode.push_back(GpuEncodeFormat{ kVideoH264, 4096, 4096, false });
	device.encode.push_back(GpuEncodeFormat{ kVideoH265, 4096, 4096, false });
	device.decode.push_back("H.264");
	device.decode.push_back("H.265");
	device.decode.push_back("VP9");
	fDevices.push_back(device);
}


/*!	The Radxa ROCK 5 ITX's SoC: the RK3588's encoder (RKVENC core 0, VEPU580)
	through Rockchip MPP and the rk3588_vpu driver; its decoders play in
	airTime and the Media Kit through the 00_rockchip_mpp add-on. */
void
HardwareInventory::_ProbeRockchip()
{
	// Linux: the MPP service node of Rockchip's kernel (air/OS: the
	// rk3588_vpu driver)
	if (access("/dev/mpp_service", F_OK) != 0)
		return;
	GpuDevice device;
	device.id = "rkvenc:/dev/mpp_service";
	device.name = "Rockchip RK3588";
	device.vendor = kVendorOther;
	device.vendorID = 0;
	device.deviceID = 0;
	device.generation = "RK3588";
	device.engine = "RKVENC (VEPU580)";
	device.backend = kBackendRockchip;
	device.devicePath = "/dev/mpp_service";
	BString why;
	device.ready = MppVideoEncoder::IsAvailable(&why);
	device.status = why;
	device.encode.push_back(GpuEncodeFormat{ kVideoH264, 8192, 8192, false });
	device.encode.push_back(GpuEncodeFormat{ kVideoH265, 8192, 8192, false });
	device.decode.push_back("H.264");
	device.decode.push_back("H.265");
	device.decode.push_back("AV1");
	device.decode.push_back("VP9");
	device.decode.push_back("MPEG-2");
	device.decode.push_back("MPEG-4");
	fDevices.push_back(device);
}


void
HardwareInventory::Probe()
{
	BAutolock _(fLock);
	fDevices.clear();
	_ProbeVaapi();
	_ProbePci();
	_ProbeIris();
	_ProbeCedar();
	_ProbeRockchip();
	// A test hook: an NVIDIA card with this PCI device ID, to try the NVENC
	// paths with the stand-in add-on (nvenc/test/fake_nvenc.cpp).
	if (const char* fake = getenv("MEDIACONVERTER_FAKE_NVIDIA"))
		_AddTableDevice(kNVIDIAVendor, (uint16)strtoul(fake, NULL, 16));
	fProbed = true;
}


int32
HardwareInventory::CountDevices() const
{
	return (int32)fDevices.size();
}


const GpuDevice&
HardwareInventory::DeviceAt(int32 index) const
{
	return fDevices[index];
}


const GpuDevice*
HardwareInventory::EncoderFor(video_codec codec, int width, int height,
	const char* preferred) const
{
	if (preferred != NULL && preferred[0] != '\0') {
		for (size_t i = 0; i < fDevices.size(); i++) {
			if (fDevices[i].id == preferred
				&& fDevices[i].CanEncode(codec, width, height))
				return &fDevices[i];
		}
	}
	for (size_t i = 0; i < fDevices.size(); i++) {
		if (fDevices[i].CanEncode(codec, width, height))
			return &fDevices[i];
	}
	return NULL;
}


BString
HardwareInventory::WhyNotHardware(video_codec codec, int width,
	int height) const
{
	const VideoCodecInfo& info = video_codec_info(codec);
	BString reason;
	if (fDevices.empty()) {
		reason = "This computer has no GPU with a video encoder air/OS can "
			"use.";
		return reason;
	}
	for (size_t i = 0; i < fDevices.size(); i++) {
		const GpuDevice& device = fDevices[i];
		const GpuEncodeFormat* format = device.Format(codec);
		BString line;
		if (format == NULL)
			line.SetToFormat("%s cannot encode %s.", device.name.String(),
				info.name);
		else if (!device.ready)
			line.SetToFormat("%s: %s", device.name.String(),
				device.status.String());
		else if ((format->maxWidth > 0 && width > format->maxWidth)
			|| (format->maxHeight > 0 && height > format->maxHeight)) {
			line.SetToFormat("%s encodes %s up to %dx%d.",
				device.name.String(), info.name, format->maxWidth,
				format->maxHeight);
		} else {
			bool works;
			BString why;
			if (device.backend == kBackendNVENC
				&& NvencEncoder::Tested(codec, &works, &why) && !works) {
				line.SetToFormat("%s: %s", device.name.String(), why.String());
			} else {
				line.SetToFormat("MediaConverter cannot use %s for %s yet.",
					device.name.String(), info.name);
			}
		}
		if (!reason.IsEmpty())
			reason << " ";
		reason << line;
	}
	return reason;
}


bool
HardwareInventory::HasHardwareEncoder() const
{
	for (size_t i = 0; i < fDevices.size(); i++) {
		if (fDevices[i].ready && !fDevices[i].encode.empty())
			return true;
	}
	return false;
}


VideoEncoder*
HardwareInventory::CreateEncoder(const GpuDevice& device)
{
	switch (device.backend) {
		case kBackendVAAPI:
			return new(std::nothrow) VaapiEncoder(device.devicePath.String(),
				device.name.String());
		case kBackendNVENC:
			return new(std::nothrow) NvencEncoder(device.name.String());
		case kBackendIris:
			return new(std::nothrow) IrisVideoEncoder(device.name.String());
		case kBackendCedar:
			return new(std::nothrow) CedarVideoEncoder(device.name.String());
		case kBackendRockchip:
			return new(std::nothrow) MppVideoEncoder(device.name.String());
		default:
			return NULL;
	}
}
