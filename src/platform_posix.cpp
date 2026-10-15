// platform_posix.cpp - Linux implementation of platform.h.
#include "platform.h"
#include <portable-file-dialogs.h>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <system_error>

namespace platform {

void initProcess() {}
void shutdownProcess() {}

bool nativeDialogsAvailable() {
    static const bool available = pfd::settings::available();
    return available;
}

void showMessage(MessageKind kind, const char* text, const char* title) {
    // Always log to stderr too: the message must survive a missing dialog tool.
    std::fprintf(stderr, "%s: %s: %s\n", kind == MessageKind::Error ? "error" : "warning",
                 title ? title : "", text ? text : "");
    if (!nativeDialogsAvailable()) return;
    pfd::message(title ? title : "", text ? text : "", pfd::choice::ok,
                 kind == MessageKind::Error ? pfd::icon::error : pfd::icon::warning).result();
}

namespace {
// pfd filters are a flat {description, "pattern [pattern...]", ...} list.
std::vector<std::string> toPfdFilters(const std::vector<FileFilter>& filters) {
    std::vector<std::string> out;
    for (const FileFilter& f : filters) {
        out.push_back(f.description ? f.description : "");
        std::string pattern = f.pattern ? f.pattern : "*";
        out.push_back(pattern == "*.*" ? "*" : pattern);   // Windows' "all files" idiom
    }
    return out;
}
} // namespace

std::filesystem::path openFileDialog(const std::vector<FileFilter>& filters) {
    if (!nativeDialogsAvailable()) return {};
    std::vector<std::string> picked =
        pfd::open_file("Open", "", toPfdFilters(filters), pfd::opt::none).result();
    return picked.empty() ? std::filesystem::path() : fromUtf8(picked.front());
}

std::filesystem::path saveFileDialog(const FileFilter& filter, const char* defaultExtension) {
    if (!nativeDialogsAvailable()) return {};
    std::string picked = pfd::save_file("Save", "", toPfdFilters({ filter }), pfd::opt::none).result();
    if (picked.empty()) return {};
    return withDefaultExtension(fromUtf8(picked), defaultExtension);
}

std::filesystem::path pickFolderDialog() {
    if (!nativeDialogsAvailable()) return {};
    std::string picked = pfd::select_folder("Choose a folder", "", pfd::opt::none).result();
    return picked.empty() ? std::filesystem::path() : fromUtf8(picked);
}

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
