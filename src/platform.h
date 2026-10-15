// platform.h - the small OS-specific surface of the app: process setup,
// message boxes, native file dialogs, and executable-relative paths.
// Implemented in platform_win32.cpp (Windows) and platform_posix.cpp (Linux).
// All strings crossing this interface are UTF-8; paths are std::filesystem.
#pragma once
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace platform {

// Per-process setup/teardown (Windows: Per-Monitor-V2 DPI awareness and COM
// for the file dialogs). Call once at the start/end of main.
void initProcess();
void shutdownProcess();

enum class MessageKind { Error, Warning };

// Blocking, modal message box.
void showMessage(MessageKind kind, const char* text, const char* title);

struct FileFilter {
    const char* description;   // e.g. "PNG image"
    const char* pattern;       // e.g. "*.png"
};

// Native dialogs. Each returns an empty path when the user cancels.
std::filesystem::path openFileDialog(const std::vector<FileFilter>& filters);
std::filesystem::path saveFileDialog(const FileFilter& filter, const char* defaultExtension);
std::filesystem::path pickFolderDialog();

// Directory containing the running executable.
std::filesystem::path exeDir();

// Current process id (used for unique scratch file names).
unsigned processId();

// fopen() for any path, including non-ASCII ones (Windows needs _wfopen).
FILE* openFile(const std::filesystem::path& path, const char* mode);

// Thread-safe localtime(). Returns false on failure.
bool localTime(std::time_t t, std::tm& out);

// UTF-8 <-> path conversions (lossless on every platform).
inline std::string toUtf8(const std::filesystem::path& p) {
    std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
inline std::filesystem::path fromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

} // namespace platform
