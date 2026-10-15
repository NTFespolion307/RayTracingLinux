#include "mmap_image.h"
#include <chrono>
#include <cstdio>

MmapImage::~MmapImage() { destroy(); }

std::filesystem::path MmapImage::resolveFolder(const std::filesystem::path& folder) {
    if (!folder.empty()) return folder;
    wchar_t buf[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n == 0 || n > MAX_PATH) return ".";
    return std::filesystem::path(buf);
}

bool MmapImage::freeSpace(const std::filesystem::path& folder, uint64_t& freeBytes) {
    std::filesystem::path dir = resolveFolder(folder);
    ULARGE_INTEGER freeForCaller{}, total{}, totalFree{};
    if (!GetDiskFreeSpaceExW(dir.c_str(), &freeForCaller, &total, &totalFree))
        return false;
    freeBytes = freeForCaller.QuadPart;
    return true;
}

bool MmapImage::create(const std::filesystem::path& folder, uint32_t width, uint32_t height) {
    destroy();
    width_ = width;
    height_ = height;

    // Unique scratch filename so concurrent runs don't collide.
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    char name[64];
    std::snprintf(name, sizeof(name), "VkGigaTracer_%08X_%08X.scratch",
                  (unsigned)(now & 0xFFFFFFFF), (unsigned)GetCurrentProcessId());
    path_ = resolveFolder(folder) / name;

    file_ = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE,
                        0, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                        nullptr);
    if (file_ == INVALID_HANDLE_VALUE) { width_ = height_ = 0; return false; }

    uint64_t size = sizeBytes();
    DWORD hi = (DWORD)(size >> 32);
    DWORD lo = (DWORD)(size & 0xFFFFFFFF);

    mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READWRITE, hi, lo, nullptr);
    if (!mapping_) { destroy(); return false; }

    base_ = (uint8_t*)MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!base_) { destroy(); return false; }
    return true;
}

void MmapImage::destroy() {
    if (base_) { UnmapViewOfFile(base_); base_ = nullptr; }
    if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
    if (file_ != INVALID_HANDLE_VALUE) { CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
    // FILE_FLAG_DELETE_ON_CLOSE reclaims the scratch file automatically.
    width_ = height_ = 0;
    path_.clear();
}
