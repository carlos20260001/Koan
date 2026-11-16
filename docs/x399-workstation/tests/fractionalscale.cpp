// Exercise fractional drawing at each pixel phase and capture device pixels.
// Usage: fractionalscale output.ppm [one-pixel moves] [render scale]
// SPDX-License-Identifier: MIT
#include <Application.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <DirectWindow.h>
#include <GradientLinear.h>
#include <IconUtils.h>
#include <Locker.h>
#include <Message.h>
#include <View.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static color_space
IconFormat()
{
	const char* format = getenv("FRACTIONAL_ICON_FORMAT");
	if (format != NULL && strcmp(format, "rgb") == 0)
		return B_RGB32;
	if (format != NULL && strcmp(format, "cmap8") == 0)
		return B_CMAP8;
	return B_RGBA32;
}


class TestView : public BView {
public:
	TestView(BRect frame, float scale) : BView(frame, "test", B_FOLLOW_ALL, B_WILL_DRAW),
		fIcon(BRect(0, 0, 31, 31), IconFormat()),
		fReference(BRect(0, 0, 32 * scale - 1, 32 * scale - 1), IconFormat()),
		fButton("icon", "", NULL), fReferenceButton("reference", "", NULL), fMutated(false)
	{
		SetViewColor(216, 216, 216);
		BIconUtils::GetSystemIcon("dialog-information", &fIcon);
		BIconUtils::GetSystemIcon("dialog-information", &fReference);
		fCopy = new BBitmap(fIcon);
		BMessage archive;
		fIcon.Archive(&archive);
		fArchive = new BBitmap(&archive);
		fImported = new BBitmap(fIcon.Bounds(), B_RGBA32);
		fImported->ImportBits(&fIcon);
		fReferenceImported = new BBitmap(fReference.Bounds(), B_RGBA32);
		fReferenceImported->ImportBits(&fReference);
		uint32 flags = B_CREATE_DISABLED_ICON_BITMAPS | B_CREATE_ACTIVE_ICON_BITMAP;
		fButton.SetIcon(&fIcon, flags);
		fReferenceButton.SetIcon(&fReference, flags);
	}

	~TestView()
	{
		delete fCopy;
		delete fArchive;
		delete fImported;
		delete fReferenceImported;
	}

	void Draw(BRect update)
	{
		SetHighColor(0, 0, 0);
		DrawString("Borders, gradients, controls and vector icons", BPoint(10, 18));
		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				BRect r(10 + 51 * x, 30 + 27 * y, 30 + 51 * x, 50 + 27 * y);
				SetHighColor(0, 0, 0);
				StrokeRect(r);
				r.InsetBy(1, 1);
				BGradientLinear gradient(r.LeftTop(), r.LeftBottom());
				gradient.AddColor(make_color(255, 255, 255), 0);
				gradient.AddColor(make_color(230, 230, 230), 255);
				FillRect(r, gradient);
				BRect box(240 + 43 * x, 30 + 27 * y, 253 + 43 * x, 43 + 27 * y);
				be_control_look->DrawCheckBox(this, box, update, ViewColor(),
					x % 2 ? BControlLook::B_ACTIVATED : 0);
			}
		}
		for (int x = 0; x < 4; x++) {
			BRect r(10 + 101 * x, 160, 100 + 101 * x, 190);
			be_control_look->DrawActiveTab(this, r, update, ViewColor(), 0,
				BControlLook::B_ALL_BORDERS, BControlLook::B_TOP_BORDER);
		}
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		const BBitmap* icons[] = { &fIcon, fCopy, fArchive, fImported,
			fButton.IconBitmap(B_INACTIVE_ICON_BITMAP),
			fButton.IconBitmap(B_DISABLED_ICON_BITMAP),
			fButton.IconBitmap(B_ACTIVE_ICON_BITMAP),
			fButton.IconBitmap(B_ACTIVE_ICON_BITMAP | B_DISABLED_ICON_BITMAP) };
		for (int x = 0; x < 8; x++) {
			DrawBitmap(icons[x], BPoint(8 + 52 * x, 208));
			const BBitmap* reference = fMutated ? &fIcon : &fReference;
			if (x == 3)
				reference = fReferenceImported;
			if (x >= 4) {
				uint32 which[] = { B_INACTIVE_ICON_BITMAP, B_DISABLED_ICON_BITMAP,
					B_ACTIVE_ICON_BITMAP, B_ACTIVE_ICON_BITMAP | B_DISABLED_ICON_BITMAP };
				reference = fReferenceButton.IconBitmap(which[x - 4]);
			}
			DrawBitmap(reference, BRect(8 + 52 * x, 252, 39 + 52 * x, 283));
		}
		SetDrawingMode(B_OP_COPY);
	}

	void UpdateScale(float scale)
	{
		fReference = BBitmap(BRect(0, 0, 32 * scale - 1, 32 * scale - 1), IconFormat());
		BIconUtils::GetSystemIcon("dialog-information", &fReference);
		delete fReferenceImported;
		fReferenceImported = new BBitmap(fReference.Bounds(), B_RGBA32);
		fReferenceImported->ImportBits(&fReference);
		fReferenceButton.SetIcon(&fReference,
			B_CREATE_DISABLED_ICON_BITMAPS | B_CREATE_ACTIVE_ICON_BITMAP);
		Invalidate();
	}

	void Mutate()
	{
		for (int y = 8; y < 16; y++) {
			if (fIcon.ColorSpace() == B_CMAP8) {
				memset((uint8*)fIcon.Bits() + y * fIcon.BytesPerRow() + 8, 1, 8);
				continue;
			}
			uint32* p = (uint32*)((uint8*)fIcon.Bits() + y * fIcon.BytesPerRow());
			for (int x = 8; x < 16; x++)
				p[x] = 0xffff00ff;
		}
		delete fCopy;
		fCopy = new BBitmap(fIcon);
		delete fArchive;
		BMessage archive;
		fIcon.Archive(&archive);
		fArchive = new BBitmap(&archive);
		fImported->ImportBits(&fIcon);
		uint32 flags = B_CREATE_DISABLED_ICON_BITMAPS | B_CREATE_ACTIVE_ICON_BITMAP;
		fButton.SetIcon(&fIcon, flags);
		fReferenceButton.SetIcon(&fIcon, flags);
		delete fReferenceImported;
		fReferenceImported = new BBitmap(*fImported);
		fMutated = true;
		Invalidate();
	}

private:
	BBitmap fIcon;
	BBitmap fReference;
	BButton fButton;
	BButton fReferenceButton;
	BBitmap* fCopy;
	BBitmap* fArchive;
	BBitmap* fImported;
	BBitmap* fReferenceImported;
	bool fMutated;
};

class TestWindow : public BDirectWindow {
public:
	TestWindow(float scale) : BDirectWindow(BRect(20, 20, 459, 309), "Fractional scaling",
		B_TITLED_WINDOW, B_DIRECT_DEVICE_PIXELS | B_NOT_RESIZABLE),
		fConnected(false), fArea(-1), fBits(NULL)
	{
		BView* root = new BView(Bounds(), "root", B_FOLLOW_ALL, 0);
		AddChild(root);
		fView = new TestView(Bounds(), scale);
		root->AddChild(fView);
	}

	~TestWindow()
	{
		if (fArea >= 0)
			delete_area(fArea);
	}

	void DirectConnected(direct_buffer_info* info)
	{
		BAutolock lock(fLock);
		fConnected = (info->buffer_state & B_DIRECT_MODE_MASK) != B_DIRECT_STOP;
		if (!fConnected)
			return;
		fScale = info->device_scale / 100.0f;
		fBounds = info->window_bounds;
		if (fArea >= 0)
			delete_area(fArea);
		area_id source = info->drawing_bits_area;
		fStride = info->drawing_bytes_per_row;
		if (source < 0) {
			source = info->bits_area;
			fStride = info->bytes_per_row;
		}
		fArea = clone_area("scaling capture", (void**)&fBits, B_ANY_ADDRESS,
			B_READ_AREA, source);
		fConnected = fArea >= 0;
	}

	int Capture(const char* path)
	{
		BAutolock windowLock(this);
		BAutolock lock(fLock);
		if (!fConnected)
			return 2;
		FILE* file = fopen(path, "wb");
		if (file == NULL)
			return 2;
		fprintf(file, "P6\n%d %d\n255\n", fBounds.right - fBounds.left + 1,
			fBounds.bottom - fBounds.top + 1);
		for (int y = fBounds.top; y <= fBounds.bottom; y++) {
			for (int x = fBounds.left; x <= fBounds.right; x++) {
				const uint8* p = fBits + y * fStride + x * 4;
				uint8 rgb[] = {p[2], p[1], p[0]};
				fwrite(rgb, 1, 3, file);
			}
		}
		fclose(file);
		int failures = 0;
		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				int left = (int)floorf((Frame().left - fView->Bounds().left + 10 + 51 * x) * fScale);
				int top = (int)floorf((Frame().top - fView->Bounds().top + 30 + 27 * y) * fScale);
				int right = (int)floorf((Frame().left - fView->Bounds().left + 31 + 51 * x) * fScale) - 1;
				int bottom = (int)floorf((Frame().top - fView->Bounds().top + 51 + 27 * y) * fScale) - 1;
				int mx = (left + right) / 2, my = (top + bottom) / 2;
				int px[] = {left, mx, right, mx};
				int py[] = {my, top, my, bottom};
				for (int side = 0; side < 4; side++) {
					const uint8* p = fBits + py[side] * fStride + px[side] * 4;
					if (p[0] || p[1] || p[2]) {
						printf("FAIL frame %d,%d side %d: %d,%d,%d\n", x, y,
							side, p[2], p[1], p[0]);
						failures++;
					}
				}
			}
		}
		printf("scale %.2f: %d/64 border samples passed\n", fScale, 64 - failures);
		for (int i = 0; i < 8; i++) {
			int x0 = (int)floorf((Frame().left - fView->Bounds().left + 8 + 52 * i) * fScale);
			int y0 = (int)floorf((Frame().top - fView->Bounds().top + 208) * fScale);
			int y1 = (int)floorf((Frame().top - fView->Bounds().top + 252) * fScale);
			int size = (int)roundf(32 * fScale);
			int different = 0;
			for (int y = 0; y < size; y++) {
				for (int x = 0; x < size; x++) {
					uint8* a = fBits + (y0 + y) * fStride + (x0 + x) * 4;
					uint8* b = fBits + (y1 + y) * fStride + (x0 + x) * 4;
					if (memcmp(a, b, 3) != 0)
						different++;
				}
			}
			printf("icon %d: %d/%d pixels differ from native-size reference\n",
				i, different, size * size);
			if (different > 0)
				failures++;
		}
		return failures ? 1 : 0;
	}

	void UpdateScale(float scale)
	{
		fView->UpdateScale(scale);
	}

	void Scroll()
	{
		fView->ScrollBy(1, 1);
	}

	void Mutate()
	{
		fView->Mutate();
	}

private:
	TestView* fView;
	BLocker fLock;
	bool fConnected;
	area_id fArea;
	uint8* fBits;
	int32 fStride;
	float fScale;
	clipping_rect fBounds;
};

int main(int argc, char** argv)
{
	if (argc < 2)
		return 2;
	BApplication app("application/x-vnd.vos-fractionalscale-test");
	TestWindow* window = new TestWindow(argc > 3 ? atof(argv[3]) : 1);
	window->Show();
	snooze(1000000);
	window->Lock();
	window->Sync();
	window->Unlock();
	int result = window->Capture(argv[1]);
	fflush(stdout);
	for (int i = 0; argc > 2 && i < atoi(argv[2]); i++) {
		window->Lock();
		window->MoveBy(1, 1);
		window->Unlock();
		snooze(100000);
		char path[1024];
		snprintf(path, sizeof(path), "%s.move%d.ppm", argv[1], i + 1);
		result |= window->Capture(path);
		fflush(stdout);
	}
	for (int i = 0; argc > 2 && i < atoi(argv[2]); i++) {
		window->Lock();
		window->Scroll();
		window->Unlock();
		snooze(100000);
		char path[1024];
		snprintf(path, sizeof(path), "%s.scroll%d.ppm", argv[1], i + 1);
		result |= window->Capture(path);
		fflush(stdout);
	}
	const char* sequence = getenv("FRACTIONAL_SCALE_SEQUENCE");
	if (sequence != NULL) {
		char* scales = strdup(sequence);
		for (char* value = strtok(scales, ","); value != NULL; value = strtok(NULL, ",")) {
			int scale = atoi(value);
			if (scale < 100 || scale > 250 || scale % 25 != 0)
				return 2;
			char command[128];
			snprintf(command, sizeof(command), "screenmode --display 1 --scale %d", scale);
			if (system(command) != 0)
				return 2;
			window->Lock();
			window->UpdateScale(scale / 100.0f);
			window->Unlock();
			snooze(300000);
			char path[1024];
			snprintf(path, sizeof(path), "%s.scale%d.ppm", argv[1], scale);
			result |= window->Capture(path);
			fflush(stdout);
		}
		free(scales);
	}
	window->Lock();
	window->Mutate();
	window->Unlock();
	snooze(100000);
	char path[1024];
	snprintf(path, sizeof(path), "%s.mutated.ppm", argv[1]);
	result |= window->Capture(path);
	fflush(stdout);

	window->Lock();
	window->Quit();
	return result;
}
