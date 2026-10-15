// mmap_image.cpp - platform-independent parts of MmapImage: scratch folder
// selection, free-space queries and file naming. create()/destroy() live in
// mmap_image_win32.cpp / mmap_image_posix.cpp.
#include "mmap_image.h"
#include "platform.h"
#include <chrono>
#include <cstdio>
#include <system_error>

namespace fs = std::filesystem;

MmapImage::~MmapImage() { destroy(); }

fs::path MmapImage::chooseFolder(const fs::path& preferred, const fs::path& fallback) {
    std::error_code ec;
    fs::create_directories(preferred, ec);
    if (fs::is_directory(preferred, ec)) {
        // Probe for write access (read-only install dirs, network shares...).
        fs::path probe = preferred / ".vkgigatracer_write_test";
        if (FILE* f = platform::openFile(probe, "wb")) {
            std::fclose(f);
            fs::remove(probe, ec);
            return preferred;
        }
    }
    return fallback;
}

fs::path MmapImage::defaultFolder() {
    static const fs::path folder = [] {
        std::error_code ec;
        fs::path systemTemp = fs::temp_directory_path(ec);
        if (ec || systemTemp.empty()) systemTemp = ".";
        fs::path exe = platform::exeDir();
        if (exe.empty()) return systemTemp;
        return chooseFolder(exe / "temp", systemTemp);
    }();
    return folder;
}

fs::path MmapImage::resolveFolder(const fs::path& folder) {
    return folder.empty() ? defaultFolder() : folder;
}

bool MmapImage::freeSpace(const fs::path& folder, uint64_t& freeBytes) {
    std::error_code ec;
    fs::space_info si = fs::space(resolveFolder(folder), ec);
    if (ec) return false;
    freeBytes = si.available;
    return true;
}

fs::path MmapImage::newScratchPath(const fs::path& folder) {
    fs::path dir = resolveFolder(folder);
    if (folder.empty()) {
        // The default folder may have been deleted since it was chosen.
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
    // Unique scratch filename so concurrent runs don't collide.
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    char name[64];
    std::snprintf(name, sizeof(name), "VkGigaTracer_%08X_%08X.scratch",
                  (unsigned)(now & 0xFFFFFFFF), (unsigned)platform::processId());
    return dir / name;
}
