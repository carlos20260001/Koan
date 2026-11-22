/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "VulkanLibrary.h"

#include <dlfcn.h>
#include <new>
#include <stdlib.h>
#include <string.h>

#include <image.h>
#include <OS.h>
#include <PathFinder.h>
#include <StringList.h>
#include <unistd.h>


static const char* const kLoaderNames[] = {
	"libvulkan.so.1",
	"libvulkan.so"
};


/*!	Whether there is a library of that name where libraries are looked
	for: the lib directories of the system and the user, and what
	LIBRARY_PATH names. The runtime loader writes a line to the system log
	for every library it is asked for and does not find, and most
	machines without a graphics driver have no Vulkan loader: this keeps
	every team that uses the kit from leaving two lines there. In doubt
	the answer is yes. */
static bool
library_exists(const char* name)
{
#ifdef __linux__
	// glibc's dlopen() logs nothing when it finds no library.
	(void)name;
	return true;
#endif

	// B_ENTRY_NOT_FOUND is the answer "in none of them"
	BStringList paths;
	status_t status = BPathFinder::FindPaths(B_FIND_PATH_LIB_DIRECTORY, name,
		B_FIND_PATH_EXISTING_ONLY, paths);
	if (status != B_ENTRY_NOT_FOUND
		&& (status != B_OK || paths.CountStrings() > 0)) {
		return true;
	}

	const char* search = getenv("LIBRARY_PATH");
	if (search == NULL)
		return false;

	BStringList directories;
	BString(search).Split(":", true, directories);
	for (int32 i = 0; i < directories.CountStrings(); i++) {
		BString path = directories.StringAt(i);
		// "%A" is the directory of the program, which only the runtime
		// loader knows for sure
		if (path.FindFirst('%') >= 0)
			return true;
		path << "/" << name;
		if (access(path.String(), R_OK) == 0)
			return true;
	}
	return false;
}


VulkanLibrary::VulkanLibrary()
	:
	fHandle(NULL),
	fGetInstanceProcAddr(NULL),
	fInstance(VK_NULL_HANDLE),
	fApiVersion(VK_API_VERSION_1_0),
	fDevices(NULL),
	fDeviceCount(0)
{
	memset(&fFunctions, 0, sizeof(fFunctions));
}


VulkanLibrary::~VulkanLibrary()
{
	if (fInstance != VK_NULL_HANDLE)
		fFunctions.DestroyInstance(fInstance, NULL);
	free(fDevices);

	// The loader and the drivers it loaded stay: a driver that was running
	// threads or registered something with the C library does not survive
	// being unloaded.
}


status_t
VulkanLibrary::Init()
{
	for (size_t i = 0; fHandle == NULL
			&& i < sizeof(kLoaderNames) / sizeof(kLoaderNames[0]); i++) {
		if (library_exists(kLoaderNames[i]))
			fHandle = dlopen(kLoaderNames[i], RTLD_NOW | RTLD_LOCAL);
	}
	if (fHandle == NULL)
		return B_ENTRY_NOT_FOUND;

	fGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(fHandle,
		"vkGetInstanceProcAddr");
	if (fGetInstanceProcAddr == NULL)
		return B_MISSING_SYMBOL;

	status_t status = _CreateInstance();
	if (status != B_OK)
		return status;

	status = _FindDevices();
	if (status == B_OK)
		_FindDriverImages();
	return status;
}


status_t
VulkanLibrary::_CreateInstance()
{
	PFN_vkCreateInstance createInstance = (PFN_vkCreateInstance)
		fGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
	if (createInstance == NULL)
		return B_MISSING_SYMBOL;

	// A loader of Vulkan 1.0 does not have this function, and refuses an
	// instance of any other version.
	PFN_vkEnumerateInstanceVersion enumerateVersion
		= (PFN_vkEnumerateInstanceVersion)fGetInstanceProcAddr(
			VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
	uint32 version = VK_API_VERSION_1_0;
	if (enumerateVersion != NULL
		&& enumerateVersion(&version) == VK_SUCCESS
		&& version >= VK_API_VERSION_1_1) {
		fApiVersion = VK_API_VERSION_1_1;
	}

	VkApplicationInfo application;
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "Neural Kit";
	application.pEngineName = "vulkan.neural";
	application.apiVersion = fApiVersion;

	VkInstanceCreateInfo info;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	info.pApplicationInfo = &application;

	if (createInstance(&info, NULL, &fInstance) != VK_SUCCESS) {
		fInstance = VK_NULL_HANDLE;
		return B_ERROR;
	}

#define LOAD(name) \
	fFunctions.name = (PFN_vk##name)fGetInstanceProcAddr(fInstance, \
		"vk" #name); \
	if (fFunctions.name == NULL) \
		return B_MISSING_SYMBOL;
	VULKAN_INSTANCE_FUNCTIONS(LOAD)
#undef LOAD

	if (fApiVersion >= VK_API_VERSION_1_1) {
#define LOAD(name) \
		fFunctions.name = (PFN_vk##name)fGetInstanceProcAddr(fInstance, \
			"vk" #name);
		VULKAN_INSTANCE_FUNCTIONS_1_1(LOAD)
#undef LOAD
	}

	return B_OK;
}


status_t
VulkanLibrary::_FindDevices()
{
	uint32 count = 0;
	if (fFunctions.EnumeratePhysicalDevices(fInstance, &count, NULL)
			!= VK_SUCCESS || count == 0) {
		return B_OK;
	}

	VkPhysicalDevice* handles = (VkPhysicalDevice*)calloc(count,
		sizeof(VkPhysicalDevice));
	fDevices = (VulkanPhysicalDevice*)calloc(count,
		sizeof(VulkanPhysicalDevice));
	if (handles == NULL || fDevices == NULL) {
		free(handles);
		return B_NO_MEMORY;
	}

	VkResult result = fFunctions.EnumeratePhysicalDevices(fInstance, &count,
		handles);
	if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
		for (uint32 i = 0; i < count; i++) {
			if (_InitDevice(handles[i], fDevices[fDeviceCount]))
				fDeviceCount++;
		}
	}

	free(handles);
	return B_OK;
}


/*!	Whether the device is one for the back end: a graphics processor (a
	Vulkan implementation on the CPU would only be a slower CPU device)
	with a queue that takes compute work. */
bool
VulkanLibrary::_InitDevice(VkPhysicalDevice handle,
	VulkanPhysicalDevice& device)
{
	memset(&device, 0, sizeof(device));
	device.handle = handle;

	fFunctions.GetPhysicalDeviceProperties(handle, &device.properties);
	VkPhysicalDeviceType type = device.properties.deviceType;
	if (type != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
		&& type != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
		&& type != VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) {
		return false;
	}

	// the queue: one for compute only, if there is a choice
	uint32 familyCount = 0;
	fFunctions.GetPhysicalDeviceQueueFamilyProperties(handle, &familyCount,
		NULL);
	if (familyCount == 0)
		return false;
	VkQueueFamilyProperties* families = (VkQueueFamilyProperties*)calloc(
		familyCount, sizeof(VkQueueFamilyProperties));
	if (families == NULL)
		return false;
	fFunctions.GetPhysicalDeviceQueueFamilyProperties(handle, &familyCount,
		families);

	int32 family = -1;
	for (uint32 i = 0; i < familyCount; i++) {
		if (families[i].queueCount == 0
			|| (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) {
			continue;
		}
		if (family < 0 || ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0
				&& (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)
					!= 0)) {
			family = i;
		}
	}
	free(families);
	if (family < 0)
		return false;
	device.queue_family = family;

	// the memory: the largest heap of the device's own
	fFunctions.GetPhysicalDeviceMemoryProperties(handle, &device.memory);
	bool found = false;
	for (uint32 i = 0; i < device.memory.memoryTypeCount; i++) {
		const VkMemoryType& memoryType = device.memory.memoryTypes[i];
		if ((memoryType.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
				== 0) {
			continue;
		}
		uint64 size = device.memory.memoryHeaps[memoryType.heapIndex].size;
		if (!found || size > device.local_heap_size) {
			device.local_heap = memoryType.heapIndex;
			device.local_heap_size = size;
			found = true;
		}
	}
	if (!found)
		return false;

	// what the driver says about itself, where it does
	device.max_allocation_size = (uint64)1 << 30;
		// what Vulkan 1.1 promises at least

	uint32 extensionCount = 0;
	bool hasDriverProperties = false;
	if (fFunctions.EnumerateDeviceExtensionProperties(handle, NULL,
			&extensionCount, NULL) == VK_SUCCESS && extensionCount > 0) {
		VkExtensionProperties* extensions = (VkExtensionProperties*)calloc(
			extensionCount, sizeof(VkExtensionProperties));
		if (extensions != NULL) {
			VkResult result = fFunctions.EnumerateDeviceExtensionProperties(
				handle, NULL, &extensionCount, extensions);
			if (result != VK_SUCCESS && result != VK_INCOMPLETE)
				extensionCount = 0;
			for (uint32 i = 0; i < extensionCount; i++) {
				const char* name = extensions[i].extensionName;
				if (strcmp(name, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
					device.has_memory_budget = true;
				if (strcmp(name, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)
						== 0) {
					hasDriverProperties = true;
				}
			}
			free(extensions);
		}
	}

	if (fFunctions.GetPhysicalDeviceProperties2 != NULL
		&& device.properties.apiVersion >= VK_API_VERSION_1_1) {
		VkPhysicalDeviceMaintenance3Properties maintenance;
		memset(&maintenance, 0, sizeof(maintenance));
		maintenance.sType
			= VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;

		VkPhysicalDeviceDriverProperties driver;
		memset(&driver, 0, sizeof(driver));
		driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
		if (hasDriverProperties)
			maintenance.pNext = &driver;

		VkPhysicalDeviceProperties2 properties;
		memset(&properties, 0, sizeof(properties));
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties.pNext = &maintenance;
		fFunctions.GetPhysicalDeviceProperties2(handle, &properties);

		if (maintenance.maxMemoryAllocationSize > 0)
			device.max_allocation_size = maintenance.maxMemoryAllocationSize;
		if (hasDriverProperties) {
			strlcpy(device.driver_name, driver.driverName,
				sizeof(device.driver_name));
			strlcpy(device.driver_info, driver.driverInfo,
				sizeof(device.driver_info));
			device.driver_id = driver.driverID;
		}
	} else
		device.has_memory_budget = false;

	return true;
}


/*!	Notes the libraries in this team that are Vulkan drivers: those that
	have the function the loader calls first. */
void
VulkanLibrary::_FindDriverImages()
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		void* symbol;
		if (get_image_symbol(info.id, "vk_icdGetInstanceProcAddr",
				B_SYMBOL_TYPE_TEXT, &symbol) == B_OK
			|| get_image_symbol(info.id,
				"vk_icdNegotiateLoaderICDInterfaceVersion",
				B_SYMBOL_TYPE_TEXT, &symbol) == B_OK) {
			fDriverPaths << info.name << "\n";
		}
	}
}


BString
VulkanLibrary::DriverPath(int32 index) const
{
	// what the libraries of the Mesa drivers are called
	static const struct {
		uint32		id;
		const char*	name;
	} kNames[] = {
		{ VK_DRIVER_ID_MESA_NVK, "nouveau" },
		{ VK_DRIVER_ID_MESA_RADV, "radeon" },
		{ VK_DRIVER_ID_MESA_TURNIP, "freedreno" },
		{ VK_DRIVER_ID_MESA_V3DV, "broadcom" },
		{ VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA, "powervr" },
		{ VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA, "intel" },
		{ VK_DRIVER_ID_MESA_PANVK, "panfrost" },
		{ VK_DRIVER_ID_MESA_HONEYKRISP, "asahi" }
	};

	const char* wanted = NULL;
	if (index >= 0 && index < fDeviceCount) {
		for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
			if (kNames[i].id == fDevices[index].driver_id)
				wanted = kNames[i].name;
		}
	}

	// the one with the name of the driver, or all there are
	BString all;
	int32 start = 0;
	while (start < fDriverPaths.Length()) {
		int32 end = fDriverPaths.FindFirst('\n', start);
		if (end < 0)
			break;
		BString path(fDriverPaths.String() + start, end - start);
		start = end + 1;

		const char* leaf = strrchr(path.String(), '/');
		leaf = leaf != NULL ? leaf + 1 : path.String();
		if (wanted != NULL && strstr(leaf, wanted) != NULL)
			return path;
		if (all.Length() > 0)
			all << ", ";
		all << path;
	}
	return all;
}
