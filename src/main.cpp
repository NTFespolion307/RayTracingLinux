// main.cpp - entry point. Performs per-process OS setup (DPI awareness and COM
// on Windows), parses --selftest, and runs the application. All Vulkan
// loading is dynamic via volk; if the Vulkan loader or a device is missing the
// App shows one friendly message box and exits cleanly.
#include "ui.h"
#include "platform.h"
#include <cstring>

namespace {
int appMain(int argc, char** argv) {
    platform::initProcess();

    bool selftest = false;
    for (int i = 1; i < argc; ++i)
        if (argv[i] && std::strcmp(argv[i], "--selftest") == 0) selftest = true;

    int rc;
    {
        App app;
        rc = selftest ? app.runSelfTest() : app.run();
    }

    platform::shutdownProcess();
    return rc;
}
} // namespace

int main(int argc, char** argv) { return appMain(argc, argv); }

#ifdef _WIN32
// The Windows build is a GUI-subsystem exe (no console), whose entry is WinMain.
#include <windows.h>
#include <cstdlib>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return appMain(__argc, __argv); }
#endif
