// mmap_image_win32.cpp - Windows create/destroy for MmapImage: a temporary,
// delete-on-close file mapped with one 64-bit MapViewOfFile.
#include "mmap_image.h"
#include <windows.h>

bool MmapImage::create(const std::filesystem::path& folder, uint32_t width, uint32_t height) {
    destroy();
    width_ = width;
    height_ = height;

    path_ = newScratchPath(folder);

    HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) { width_ = height_ = 0; path_.clear(); return false; }
    file_ = file;

    uint64_t size = sizeBytes();
    DWORD hi = (DWORD)(size >> 32);
    DWORD lo = (DWORD)(size & 0xFFFFFFFF);

    mapping_ = CreateFileMappingW(file, nullptr, PAGE_READWRITE, hi, lo, nullptr);
    if (!mapping_) { destroy(); return false; }

    base_ = (uint8_t*)MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!base_) { destroy(); return false; }
    return true;
}

void MmapImage::destroy() {
    if (base_) { UnmapViewOfFile(base_); base_ = nullptr; }
    if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
    if (file_) { CloseHandle(file_); file_ = nullptr; }
    // FILE_FLAG_DELETE_ON_CLOSE reclaims the scratch file automatically.
    width_ = height_ = 0;
    path_.clear();
}
