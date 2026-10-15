// platform_posix.cpp - Linux implementation of platform.h.
#include "platform.h"
#include <unistd.h>
#include <cstdio>
#include <system_error>

namespace platform {

void initProcess() {}
void shutdownProcess() {}

// TODO(porting step 12): native dialogs via portable-file-dialogs. Until then
// messages go to stderr and the file dialogs report "cancelled".
void showMessage(MessageKind kind, const char* text, const char* title) {
    std::fprintf(stderr, "%s: %s: %s\n", kind == MessageKind::Error ? "error" : "warning",
                 title ? title : "", text ? text : "");
}

std::filesystem::path openFileDialog(const std::vector<FileFilter>&) { return {}; }
std::filesystem::path saveFileDialog(const FileFilter&, const char*) { return {}; }
std::filesystem::path pickFolderDialog() { return {}; }

std::filesystem::path exeDir() {
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    return exe.parent_path();
}

unsigned processId() { return (unsigned)getpid(); }

FILE* openFile(const std::filesystem::path& path, const char* mode) {
    return std::fopen(path.c_str(), mode);
}

bool localTime(std::time_t t, std::tm& out) {
    return localtime_r(&t, &out) != nullptr;
}

std::filesystem::path systemUiFont(bool semibold) {
    // Common sans-serif fonts shipped by Ubuntu/Debian/Fedora, regular and bold.
    static const char* kRegular[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    };
    static const char* kBold[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
    };
    std::error_code ec;
    for (const char* p : semibold ? kBold : kRegular)
        if (std::filesystem::is_regular_file(p, ec)) return p;
    return {};
}

} // namespace platform
