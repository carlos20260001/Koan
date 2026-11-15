/*
 * Copyright 2026, Vitruvian OS.
 * Distributed under the terms of the MIT License.
 */
#ifndef UDEV_DEVICE_NAME_H
#define UDEV_DEVICE_NAME_H


#include <InputServerDevice.h>
#include <String.h>

#include <libudev.h>
#include <sys/stat.h>


static inline bool
udev_flag(struct udev_device* dev, const char* key)
{
	const char* v = udev_device_get_property_value(dev, key);
	return v != NULL && strcmp(v, "1") == 0;
}


// Fills _model with a human product name when the bus has one hwdb can
// resolve (USB, PCI), and _role with udev's own classification. Either may
// come back empty/unknown; the caller decides what to fall back to.
static inline void
udev_device_name(const char* path, BString& _model, int32& _role)
{
	_model = "";
	_role = UDEV_ROLE_UNKNOWN;

	struct stat st;
	if (path == NULL || stat(path, &st) != 0 || !S_ISCHR(st.st_mode))
		return;

	struct udev* udev = udev_new();
	if (udev == NULL)
		return;

	struct udev_device* dev
		= udev_device_new_from_devnum(udev, 'c', st.st_rdev);
	if (dev != NULL) {
		// Touchpads and tablets also set ID_INPUT_MOUSE, so they win.
		if (udev_flag(dev, "ID_INPUT_TOUCHPAD"))
			_role = UDEV_ROLE_TOUCHPAD;
		else if (udev_flag(dev, "ID_INPUT_TABLET"))
			_role = UDEV_ROLE_TABLET;
		else if (udev_flag(dev, "ID_INPUT_MOUSE"))
			_role = UDEV_ROLE_MOUSE;
		else if (udev_flag(dev, "ID_INPUT_KEYBOARD"))
			_role = UDEV_ROLE_KEYBOARD;

		const char* model
			= udev_device_get_property_value(dev, "ID_MODEL_FROM_DATABASE");
		const char* vendor
			= udev_device_get_property_value(dev, "ID_VENDOR_FROM_DATABASE");

		if (model != NULL && model[0] != '\0') {
			if (vendor != NULL && vendor[0] != '\0'
				&& strncmp(model, vendor, strlen(vendor)) != 0) {
				_model = vendor;
				_model << ' ' << model;
			} else
				_model = model;
		}

		udev_device_unref(dev);
	}

	udev_unref(udev);
}


#endif	// UDEV_DEVICE_NAME_H
