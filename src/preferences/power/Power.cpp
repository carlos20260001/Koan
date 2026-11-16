/* Copyright 2026 air/OS contributors. Distributed under the MIT License. */
#include <Application.h>
#include <Box.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <File.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include <driver_settings.h>
#include <stdlib.h>
#include <string.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PowerWindow"

// What the power daemon reads (src/servers/power/power_policy.cpp).
static const char* kSettingsName = "system/power";

static const uint32 kChanged = 'pchg';

static const char* kLidActions[] = { "sleep", "display_off", "nothing" };

// minutes with the lid closed before the sleep gets deeper, 0 never
static const int32 kDeepSleepMinutes[] = { 5, 15, 30, 60, 120, 240, 0 };
static const int32 kDefaultDeepSleepMinutes = 30;


class PowerWindow : public BWindow {
public:
	PowerWindow()
		:
		BWindow(BRect(0, 0, 400, 300), B_TRANSLATE_SYSTEM_NAME("Power"),
			B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
				| B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
	{
		fBatterySaving = new BCheckBox("battery saving",
			B_TRANSLATE("Save power: slower, but the battery lasts longer"),
			new BMessage(kChanged));
		fBatteryBrightness = new BSlider("battery brightness",
			B_TRANSLATE("Display brightness at most:"), new BMessage(kChanged),
			10, 100, B_HORIZONTAL);
		fBatteryBrightness->SetModificationMessage(new BMessage(kChanged));
		fBatteryBrightness->SetLimitLabels("10%", "100%");
		fBatteryBrightness->SetHashMarks(B_HASH_MARKS_BOTTOM);
		fBatteryBrightness->SetHashMarkCount(10);
		fDimOnBattery = new BCheckBox("dim on battery",
			B_TRANSLATE("Dim the display when unplugged"), new BMessage(kChanged));

		fACSaving = new BCheckBox("ac saving",
			B_TRANSLATE("Save power: slower, but quieter and cooler"),
			new BMessage(kChanged));

		BPopUpMenu* lidMenu = new BPopUpMenu("lid action");
		lidMenu->AddItem(new BMenuItem(B_TRANSLATE("Sleep"),
			new BMessage(kChanged)));
		lidMenu->AddItem(new BMenuItem(B_TRANSLATE("Turn off the display"),
			new BMessage(kChanged)));
		lidMenu->AddItem(new BMenuItem(B_TRANSLATE("Do nothing"),
			new BMessage(kChanged)));
		fLidAction = new BMenuField("lid action",
			B_TRANSLATE("When the lid is closed:"), lidMenu);

		fExternalAwake = new BCheckBox("external awake",
			B_TRANSLATE("Keep working on external displays"),
			new BMessage(kChanged));
		fExternalAwake->SetToolTip(B_TRANSLATE("With the lid closed while "
			"an external display is connected, the computer stays awake and "
			"only its own display turns off. When the external displays go "
			"away, the lid setting above applies."));

		BPopUpMenu* deepMenu = new BPopUpMenu("deep sleep");
		for (int32 i = 0; i < (int32)B_COUNT_OF(kDeepSleepMinutes); i++) {
			int32 minutes = kDeepSleepMinutes[i];
			BString label;
			if (minutes == 0)
				label = B_TRANSLATE("Never");
			else if (minutes < 60) {
				label.SetToFormat(B_TRANSLATE("%d minutes"), (int)minutes);
			} else if (minutes == 60)
				label = B_TRANSLATE("1 hour");
			else {
				label.SetToFormat(B_TRANSLATE("%d hours"),
					(int)(minutes / 60));
			}
			deepMenu->AddItem(new BMenuItem(label, new BMessage(kChanged)));
		}
		fDeepSleep = new BMenuField("deep sleep",
			B_TRANSLATE("Sleep deeper after:"), deepMenu);
		fDeepSleep->SetToolTip(B_TRANSLATE("After the lid has been closed "
			"this long, the graphics go to sleep, too: the battery lasts "
			"longer, waking up takes a little longer."));

		BBox* battery = new BBox("battery");
		battery->SetLabel(B_TRANSLATE("On battery"));
		BLayoutBuilder::Group<>(battery, B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
				B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
			.Add(fBatterySaving)
			.Add(fDimOnBattery)
			.Add(fBatteryBrightness);

		BBox* ac = new BBox("ac");
		ac->SetLabel(B_TRANSLATE("Plugged in"));
		BLayoutBuilder::Group<>(ac, B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
				B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
			.Add(fACSaving);

		BBox* lid = new BBox("lid");
		lid->SetLabel(B_TRANSLATE("Lid"));
		BLayoutBuilder::Grid<>(lid, B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
				B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
			.AddMenuField(fLidAction, 0, 0)
			.Add(fExternalAwake, 1, 1)
			.AddMenuField(fDeepSleep, 0, 2);

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(battery)
			.Add(ac)
			.Add(lid);

		_Load();
		CenterOnScreen();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kChanged) {
			fBatteryBrightness->SetEnabled(
				fDimOnBattery->Value() == B_CONTROL_ON);
			_UpdateDeepSleep();
			// (the slider sends one while it is dragged; the daemon only
			// reads the file when the power source changes)
			_Save();
		} else
			BWindow::MessageReceived(message);
	}

private:
	void _UpdateDeepSleep()
	{
		// (only sleeping can get deeper)
		int32 lid = fLidAction->Menu()->IndexOf(
			fLidAction->Menu()->FindMarked());
		fDeepSleep->SetEnabled(lid == 0);
		fExternalAwake->SetEnabled(lid != 2);
	}

	static bool _SettingsPath(BPath& path)
	{
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
			return false;
		return path.Append(kSettingsName) == B_OK;
	}

	void _Load()
	{
		bool batterySaving = true;
		bool acSaving = false;
		int32 brightness = 70;
		const char* lid = "sleep";
		bool externalAwake = true;
		int32 deepSleep = kDefaultDeepSleepMinutes;

		BPath path;
		void* handle = _SettingsPath(path)
			? load_driver_settings(path.Path()) : NULL;
		if (handle != NULL) {
			batterySaving = get_driver_boolean_parameter(handle,
				"power_saving_on_battery", true, true);
			acSaving = get_driver_boolean_parameter(handle,
				"power_saving_on_ac", false, true);
			const char* value = get_driver_parameter(handle,
				"battery_brightness", NULL, NULL);
			if (value != NULL)
				brightness = atoi(value);
			value = get_driver_parameter(handle, "lid_closed", NULL, NULL);
			if (value != NULL)
				lid = value;
			value = get_driver_parameter(handle, "lid_closed_external_display",
				NULL, NULL);
			if (value != NULL)
				externalAwake = strcmp(value, "same") != 0;
			value = get_driver_parameter(handle, "deep_sleep_minutes", NULL,
				NULL);
			if (value != NULL)
				deepSleep = atoi(value);
		}

		fBatterySaving->SetValue(batterySaving);
		fACSaving->SetValue(acSaving);
		fDimOnBattery->SetValue(brightness > 0);
		fBatteryBrightness->SetValue(brightness > 0 ? brightness : 70);
		fBatteryBrightness->SetEnabled(brightness > 0);
		int32 index = 0;
		for (int32 i = 0; i < (int32)B_COUNT_OF(kLidActions); i++) {
			if (strcmp(lid, kLidActions[i]) == 0)
				index = i;
		}
		BMenuItem* item = fLidAction->Menu()->ItemAt(index);
		if (item != NULL)
			item->SetMarked(true);
		fExternalAwake->SetValue(externalAwake);

		// the nearest choice
		index = 0;
		for (int32 i = 0; i < (int32)B_COUNT_OF(kDeepSleepMinutes); i++) {
			int32 minutes = kDeepSleepMinutes[i];
			if ((deepSleep <= 0 && minutes == 0)
				|| (deepSleep > 0 && minutes > 0 && minutes <= deepSleep)) {
				index = i;
			}
		}
		item = fDeepSleep->Menu()->ItemAt(index);
		if (item != NULL)
			item->SetMarked(true);
		_UpdateDeepSleep();

		if (handle != NULL)
			unload_driver_settings(handle);
	}

	void _Save()
	{
		BPath path;
		if (!_SettingsPath(path))
			return;
		BPath parent;
		if (path.GetParent(&parent) == B_OK)
			create_directory(parent.Path(), 0755);

		int32 lid = fLidAction->Menu()->IndexOf(
			fLidAction->Menu()->FindMarked());
		if (lid < 0 || lid >= (int32)B_COUNT_OF(kLidActions))
			lid = 0;
		int32 deepSleep = fDeepSleep->Menu()->IndexOf(
			fDeepSleep->Menu()->FindMarked());
		deepSleep = deepSleep >= 0
				&& deepSleep < (int32)B_COUNT_OF(kDeepSleepMinutes)
			? kDeepSleepMinutes[deepSleep] : kDefaultDeepSleepMinutes;

		BString text;
		text << "# Written by the Power preferences, read by the power daemon\n"
			<< "power_saving_on_battery "
			<< (fBatterySaving->Value() == B_CONTROL_ON ? "true" : "false")
			<< "\nbattery_brightness "
			<< (fDimOnBattery->Value() == B_CONTROL_ON
				? fBatteryBrightness->Value() : 0)
			<< "\npower_saving_on_ac "
			<< (fACSaving->Value() == B_CONTROL_ON ? "true" : "false")
			<< "\nlid_closed " << kLidActions[lid]
			<< "\nlid_closed_external_display "
			<< (fExternalAwake->Value() == B_CONTROL_ON ? "awake" : "same")
			<< "\ndeep_sleep_minutes " << deepSleep << "\n";

		BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		if (file.InitCheck() == B_OK)
			file.Write(text.String(), text.Length());
	}

	BCheckBox* fBatterySaving;
	BCheckBox* fDimOnBattery;
	BSlider* fBatteryBrightness;
	BCheckBox* fACSaving;
	BMenuField* fLidAction;
	BCheckBox* fExternalAwake;
	BMenuField* fDeepSleep;
};


class PowerApplication : public BApplication {
public:
	PowerApplication() : BApplication("application/x-vnd.vos-Power") {}

	virtual void ReadyToRun()
	{
		(new PowerWindow)->Show();
	}
};


int
main()
{
	PowerApplication app;
	app.Run();
	return 0;
}
