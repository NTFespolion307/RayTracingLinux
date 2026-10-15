// mmap_image.h - disk-backed RGBA8 image accessed through a single 64-bit
// file mapping (MapViewOfFile on Windows, mmap on Linux). This is the backing
// store for the full-resolution render; its size is bounded only by free disk
// space, never by RAM or VRAM.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

class MmapImage {
public:
    MmapImage() = default;
    ~MmapImage();

    MmapImage(const MmapImage&) = delete;
    MmapImage& operator=(const MmapImage&) = delete;

    // Creates a width*height*4 byte file in `folder` (empty => the default
    // scratch folder) and maps it. The file is deleted automatically when the
    // image is destroyed or the process exits. Returns false on failure
    // (caller surfaces disk-space messaging itself).
    bool create(const std::filesystem::path& folder, uint32_t width, uint32_t height);
    void destroy();

    bool valid() const { return base_ != nullptr; }
    uint32_t width()  const { return width_; }
    uint32_t height() const { return height_; }
    uint64_t sizeBytes() const { return (uint64_t)width_ * height_ * 4ull; }

    // Pointer to the first byte of the given row (RGBA8).
    uint8_t* rowPtr(uint32_t y) { return base_ + (uint64_t)y * width_ * 4ull; }
    const uint8_t* rowPtr(uint32_t y) const { return base_ + (uint64_t)y * width_ * 4ull; }
    uint8_t* data() { return base_; }

    const std::filesystem::path& path() const { return path_; }

    // Required free bytes for a given resolution.
    static uint64_t requiredBytes(uint32_t w, uint32_t h) { return (uint64_t)w * h * 4ull; }

    // Queries free bytes available on the volume that would hold `folder`.
    static bool freeSpace(const std::filesystem::path& folder, uint64_t& freeBytes);
    // Returns the effective scratch folder: `folder` itself, or the default
    // scratch folder if `folder` is empty.
    static std::filesystem::path resolveFolder(const std::filesystem::path& folder);
    // The default scratch folder: <exe dir>/temp, or the system temp folder if
    // that can't be created or written. Computed once per process.
    static std::filesystem::path defaultFolder();
    // Returns `preferred` if it exists (or can be created) and is writable,
    // otherwise `fallback`. Exposed for tests.
    static std::filesystem::path chooseFolder(const std::filesystem::path& preferred,
                                              const std::filesystem::path& fallback);

private:
    // Unique scratch file path inside resolveFolder(folder).
    static std::filesystem::path newScratchPath(const std::filesystem::path& folder);

#ifdef _WIN32
    void*    file_    = nullptr;   // HANDLE, nullptr when closed
    void*    mapping_ = nullptr;   // HANDLE
#else
    int      fd_      = -1;
#endif
    uint8_t* base_    = nullptr;
    uint32_t width_   = 0;
    uint32_t height_  = 0;
    std::filesystem::path path_;
};
