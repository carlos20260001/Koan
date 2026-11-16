// SPDX-License-Identifier: MIT
// Preliminary external controller for the unchanged GLTeapot application.
#include <Application.h>
#include <Bitmap.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Roster.h>
#include <Screen.h>
#include <image.h>
#include <algorithm>
#include <sys/ioctl.h>
#include <initializer_list>
#include "CsfQueue.h"
using namespace MaliCSF;
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdexcept>
#include <string>
#include <vector>

extern char** environ;
static constexpr bigtime_t kReplyTimeout = 2000000;
static const char* kSignature = "application/x-vnd.vos-GLTeapot";
static unsigned captures;

static void Require(bool condition, const char* what)
{
    if (!condition) throw std::runtime_error(what);
}

#define CHECK(x) do { Require(bool(x), #x); } while (0)
#include "native-observer.h"
#undef CHECK

static void ValidateReply(uint32 operation, const BMessage& reply)
{
    Require(reply.what == B_REPLY, "scripting reply was not understood");
    type_code type;
    int32 count;
    status_t info = reply.GetInfo("error", &type, &count);
    // BWindow and BView omit error on successful reads. Setters and menu
    // invocations acknowledge success explicitly; callers check result types.
    if (operation == B_GET_PROPERTY && info == B_NAME_NOT_FOUND) return;
    int32 error = B_ERROR;
    Require(info == B_OK && type == B_INT32_TYPE && count == 1
        && reply.FindInt32("error", &error) == B_OK && error == B_OK,
        "scripting property failed");
}

static BMessage Request(const BMessenger& target, BMessage& request)
{
    BMessage reply;
    Require(target.SendMessage(&request, &reply, kReplyTimeout, kReplyTimeout) == B_OK,
        "scripting reply timeout or transport failure");
    ValidateReply(request.what, reply);
    return reply;
}

static __attribute__((noinline)) void SelfTestReply(uint32 operation,
    const BMessage& reply, bool& destroyed)
{
    struct Cleanup {
        bool& destroyed;
        ~Cleanup() { destroyed = true; }
    } cleanup{destroyed};
    BMessage copy(reply);
    ValidateReply(operation, copy);
}

static int SelfTest()
{
    try {
        unsigned accepted = 0, rejected = 0;
        auto check = [&](uint32 operation, const BMessage& reply, bool valid) {
            bool caught = false, destroyed = false;
            try {
                SelfTestReply(operation, reply, destroyed);
            } catch (const std::runtime_error&) {
                caught = true;
            }
            Require(destroyed && caught != valid, "scripting reply self-test");
            if (valid) accepted++; else rejected++;
        };
        BMessage reply(B_REPLY);
        check(B_GET_PROPERTY, reply, true);
        check(B_SET_PROPERTY, reply, false);
        check(B_EXECUTE_PROPERTY, reply, false);
        Require(reply.AddInt32("error", B_OK) == B_OK, "self-test setup");
        for (uint32 operation : {B_GET_PROPERTY, B_SET_PROPERTY, B_EXECUTE_PROPERTY})
            check(operation, reply, true);
        reply.what = B_MESSAGE_NOT_UNDERSTOOD;
        check(B_GET_PROPERTY, reply, false);
        reply.what = B_REPLY;
        Require(reply.AddInt32("error", B_OK) == B_OK, "self-test setup");
        check(B_GET_PROPERTY, reply, false);
        reply.MakeEmpty();
        Require(reply.AddInt32("error", B_ERROR) == B_OK, "self-test setup");
        check(B_GET_PROPERTY, reply, false);
        reply.MakeEmpty();
        Require(reply.AddString("error", "0") == B_OK, "self-test setup");
        check(B_GET_PROPERTY, reply, false);
        reply.MakeEmpty();
        reply.what = B_MESSAGE_NOT_UNDERSTOOD;
        check(B_GET_PROPERTY, reply, false);
        Require(accepted == 4 && rejected == 7, "self-test count");
        printf("ROCK5_APPLICATION_SELF_TEST_PASS accepted=%u rejected=%u cleanup=11\n",
            accepted, rejected);
        return 0;
    } catch (const std::exception& error) {
        fprintf(stderr, "ROCK5_APPLICATION_FAILURE self_test=%s\n", error.what());
        return 1;
    }
}

static BRect Frame(const BMessenger& window)
{
    BMessage request(B_GET_PROPERTY);
    request.AddSpecifier("Frame");
    BMessage reply = Request(window, request);
    BRect frame;
    Require(reply.FindRect("result", &frame) == B_OK && frame.IsValid(), "window frame");
    return frame;
}

static void Resize(const BMessenger& window, const BRect& frame)
{
    BMessage request(B_SET_PROPERTY);
    request.AddSpecifier("Frame");
    request.AddRect("data", frame);
    Request(window, request);
    bigtime_t deadline = system_time() + kReplyTimeout;
    while (Frame(window) != frame && system_time() < deadline) snooze(10000);
    Require(Frame(window) == frame, "window resize did not settle");
}

static bool Mark(const BMessenger& window, const char* item)
{
    BMessage request(B_GET_PROPERTY);
    request.AddSpecifier("Mark");
    request.AddSpecifier("MenuItem", item);
    request.AddSpecifier("Menu", "Settings");
    request.AddSpecifier("View", "main menu");
    BMessage reply = Request(window, request);
    bool marked;
    Require(reply.FindBool("result", &marked) == B_OK, "menu mark result");
    return marked;
}

static void SetMenu(const BMessenger& window, const char* item, bool wanted)
{
    if (Mark(window, item) != wanted) {
        BMessage request(B_EXECUTE_PROPERTY);
        request.AddSpecifier("MenuItem", item);
        request.AddSpecifier("Menu", "Settings");
        request.AddSpecifier("View", "main menu");
        Request(window, request);
    }
    bigtime_t deadline = system_time() + kReplyTimeout;
    while (Mark(window, item) != wanted && system_time() < deadline) snooze(10000);
    Require(Mark(window, item) == wanted, "menu invocation did not settle");
    printf("ROCK5_APPLICATION_MENU item=%s marked=%u\n", item, unsigned(wanted));
}

static BRect RenderingBounds(const BMessenger& window)
{
    BMessage request(B_GET_PROPERTY);
    request.AddSpecifier("Frame");
    request.AddSpecifier("View", "subview");
    BMessage reply = Request(window, request);
    BRect relative;
    Require(reply.FindRect("result", &relative) == B_OK && relative.IsValid(), "rendering view frame");
    BRect frame = Frame(window);
    relative.OffsetBy(frame.LeftTop());
    return relative;
}

static std::vector<unsigned char> Capture(const BMessenger& window,
    const char* directory, unsigned phase, unsigned sample)
{
    BScreen screen;
    Require(screen.IsValid(), "screen unavailable");
    BRect bounds = RenderingBounds(window);
    Require(screen.Frame().Contains(bounds), "rendering view is outside the screen");
    unsigned width = unsigned(bounds.IntegerWidth() + 1);
    unsigned height = unsigned(bounds.IntegerHeight() + 1);
    Require(width >= 128 && width <= 512 && height >= 128 && height <= 512, "capture geometry");
    BBitmap bitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
    Require(bitmap.InitCheck() == B_OK && screen.ReadBitmap(&bitmap, false, &bounds) == B_OK,
        "screen capture failed");
    std::vector<unsigned char> rgb(width * height * 3);
    unsigned coloured = 0, dark = 0, varied = 0, white = 0, yellow = 0;
    for (unsigned y = 0; y < height; y++) {
        const unsigned char* row = static_cast<const unsigned char*>(bitmap.Bits()) + y * bitmap.BytesPerRow();
        for (unsigned x = 0; x < width; x++) {
            unsigned char* pixel = rgb.data() + (y * width + x) * 3;
            for (unsigned c = 0; c < 3; c++) pixel[c] = row[x * 4 + 2 - c];
            bool black = pixel[0] <= 8 && pixel[1] <= 8 && pixel[2] <= 8;
            dark += black;
            coloured += !black;
            varied += std::max({pixel[0], pixel[1], pixel[2]})
                - std::min({pixel[0], pixel[1], pixel[2]}) > 8;
            white += pixel[0] >= 240 && pixel[1] >= 240 && pixel[2] >= 240;
            yellow += pixel[0] >= 240 && pixel[1] >= 240 && pixel[2] <= 8;
        }
    }
    Require(coloured >= 256 && dark >= width * height / 8,
        "captured view lacks the model or dark background");
    // ObjectView initializes current colour to white, but its FPS overlay can
    // leave it yellow (GL_CURRENT_BIT is not saved). With lighting disabled,
    // TriangleObject changes materials, not colour: accept either uniform fill.
    if (phase == 3)
        Require((white == coloured && varied == 0)
            || (yellow == coloured && varied == coloured), "unlit model is not uniform white or yellow");
    else
        Require(varied >= 128, "lit model lacks expected coloured shading");
    char path[1024];
    Require(snprintf(path, sizeof(path), "%s/phase%u-sample%u.ppm", directory, phase, sample)
        < int(sizeof(path)), "capture path too long");
    FILE* output = fopen(path, "wb");
    Require(output != nullptr, "capture file open");
    bool wrote = fprintf(output, "P6\n%u %u\n255\n", width, height) > 0
        && fwrite(rgb.data(), 1, rgb.size(), output) == rgb.size();
    Require(fclose(output) == 0 && wrote, "capture file write");
    printf("ROCK5_APPLICATION_CAPTURE phase=%u sample=%u width=%u height=%u coloured=%u dark=%u chromatic=%u white=%u yellow=%u file=%s\n",
        phase, sample, width, height, coloured, dark, varied, white, yellow, path);
    captures++;
    return rgb;
}

static void ObserveAnimation(const BMessenger& window, const char* directory, unsigned phase)
{
    snooze(500000);
    auto before = Capture(window, directory, phase, 0);
    snooze(500000);
    auto after = Capture(window, directory, phase, 1);
    Require(before.size() == after.size(), "animation geometry changed");
    unsigned changed = 0;
    for (size_t i = 0; i < before.size(); i += 3)
        changed += memcmp(before.data() + i, after.data() + i, 3) != 0;
    Require(changed >= 128, "application animation did not advance");
    printf("ROCK5_APPLICATION_ANIMATION phase=%u changed_pixels=%u\n", phase, changed);
}

int main(int argc, char** argv)
{
    setbuf(stdout, nullptr);
    if (argc == 2 && !strcmp(argv[1], "--self-test")) return SelfTest();
    if (argc != 4 || (strcmp(argv[1], "--native") && strcmp(argv[1], "--software")
            && strcmp(argv[1], "--system"))) {
        return 2;
    }
    bool software = !strcmp(argv[1], "--software");
    // System mode launches the application with nothing selected: the
    // libraries, the vendor file, the device and the firmware all come from
    // their installed defaults, and the GPU is used only if the driver
    // published it.
    bool system = !strcmp(argv[1], "--system");
    int observer = -1;
    Snapshot baseline{};
    thread_id child = -1;
    bool reaped = false;
    BApplication application("application/x-vnd.rock5-application-controller");
    try {
        for (const char* name : {"MESA_GL_VERSION_OVERRIDE", "MESA_GLES_VERSION_OVERRIDE",
                "MESA_LOADER_DRIVER_OVERRIDE", "LIBGL_ALWAYS_SOFTWARE", "GALLIUM_DRIVER",
                "HAIKU_CSF_DEVICE", "HAIKU_CSF_FIRMWARE", "HAIKU_CSF_TRACE"})
            Require(unsetenv(name) == 0, "clear renderer override");
        if (software) {
            Require(access("/dev/graphics/mali_csf/0", F_OK) != 0 && errno == ENOENT,
                "software mode requires the native GPU to be absent");
            Require(setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1) == 0, "software mode selection");
            Require(setenv("GALLIUM_DRIVER", "softpipe", 1) == 0, "software renderer selection");
        } else if (system) {
            for (const char* name : {"LIBRARY_PATH", "__EGL_VENDOR_LIBRARY_FILENAMES",
                    "__EGL_VENDOR_LIBRARY_DIRS", "HAIKU_PAN_SW_POLYGON", "HAIKU_PAN_SW_POLYGON_TRACE"})
                Require(unsetenv(name) == 0, "clear launch environment");
            bool present = access("/dev/graphics/mali_csf/0", F_OK) == 0;
            Require(present || errno == ENOENT, "native GPU device probe");
            printf("ROCK5_APPLICATION_SYSTEM device=%s libraries=/boot/system/non-packaged/lib"
                " vendor=/boot/system/non-packaged/add-ons/opengl/egl_vendor.d\n",
                present ? "present" : "absent");
            if (present) {
                observer = open("/dev/graphics/mali_csf/0", O_RDWR | O_CLOEXEC);
                Require(observer >= 0 && snapshot(observer, &baseline), "native allocation baseline");
                Require(same_snapshot(baseline, baseline, 99), "initial native baseline");
            }
        } else {
            Require(setenv("HAIKU_CSF_DEVICE", "/dev/graphics/mali_csf/0", 1) == 0,
                "native GPU selection");
            Require(setenv("HAIKU_CSF_FIRMWARE", "/boot/home/mali_csffw.bin", 1) == 0,
                "native GPU firmware");
            Require(setenv("HAIKU_CSF_TRACE", "1", 1) == 0, "native GPU observation");
            observer = open("/dev/graphics/mali_csf/0", O_RDWR | O_CLOEXEC);
            Require(observer >= 0 && snapshot(observer, &baseline), "native allocation baseline");
            Require(same_snapshot(baseline, baseline, 99), "initial native baseline");
        }
        Require(!be_roster->IsRunning(kSignature), "GLTeapot is already running");
        const char* arguments[] = {argv[2], nullptr};
        child = load_image(1, arguments, const_cast<const char**>(environ));
        Require(child >= 0, "load GLTeapot image");
        Require(resume_thread(child) == B_OK, "resume GLTeapot");
        BMessenger app;
        bigtime_t deadline = system_time() + 10000000;
        while (!app.IsValid() && system_time() < deadline) {
            app = BMessenger(kSignature, child);
            if (!app.IsValid()) snooze(20000);
        }
        Require(app.IsValid() && app.Team() == child, "owned application did not register");
        BMessage request(B_GET_PROPERTY);
        request.AddSpecifier("Windows");
        BMessage reply = Request(app, request);
        BMessenger window;
        Require(reply.FindMessenger("result", &window) == B_OK && window.Team() == child,
            "owned application window unavailable");
        printf("ROCK5_APPLICATION_STARTED team=%ld signature=%s\n", long(child), kSignature);
        Resize(window, BRect(80, 100, 335, 343));
        SetMenu(window, "FPS display", false);
        ObserveAnimation(window, argv[3], 0);
        SetMenu(window, "Perspective", true);
        ObserveAnimation(window, argv[3], 1);
        SetMenu(window, "Filled polygons", false);
        ObserveAnimation(window, argv[3], 2);
        SetMenu(window, "Filled polygons", true);
        SetMenu(window, "Lighting", false);
        Resize(window, BRect(80, 100, 399, 375));
        ObserveAnimation(window, argv[3], 3);
        SetMenu(window, "Lighting", true);
        SetMenu(window, "Perspective", false);
        Require(app.SendMessage(B_QUIT_REQUESTED) == B_OK, "request normal application quit");
        status_t result = B_ERROR;
        Require(wait_for_thread_etc(child, B_RELATIVE_TIMEOUT, 10000000, &result) == B_OK,
            "application did not exit within deadline");
        reaped = true;
        Require(result == 0 && captures == 8, "application exit or capture count");
        if (observer >= 0) {
            Snapshot after;
            bigtime_t deadline = system_time() + 5000000;
            do {
                Require(snapshot(observer, &after), "final native allocation query");
                if (!memcmp(&baseline, &after, sizeof(after))) break;
                snooze(10000);
            } while (system_time() < deadline);
            Require(same_snapshot(baseline, after, 0), "native application resource cleanup");
            close(observer);
            observer = -1;
        }
        printf("ROCK5_APPLICATION_CONTROLLER_PASS team=%ld captures=%u normal_exit=0\n", long(child), captures);
        return 0;
    } catch (const std::exception& error) {
        fprintf(stderr, "ROCK5_APPLICATION_FAILURE operation=%s errno=%d\n", error.what(), errno);
        if (child >= 0 && !reaped) {
            BMessenger app(kSignature, child);
            if (app.IsValid()) app.SendMessage(B_QUIT_REQUESTED);
            status_t result;
            if (wait_for_thread_etc(child, B_RELATIVE_TIMEOUT, 2000000, &result) != B_OK) {
                thread_info info;
                if (get_thread_info(child, &info) == B_OK && info.team == child) kill_team(child);
            }
        }
        if (observer >= 0) close(observer);
        return 1;
    }
}
