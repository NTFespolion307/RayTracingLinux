// mmap_image_posix.cpp - Linux create/destroy for MmapImage: a file whose
// blocks are reserved up front with posix_fallocate, unlinked immediately
// (the equivalent of Windows' delete-on-close: the space is reclaimed when the
// descriptor closes, even after a crash), and mapped with one MAP_SHARED mmap.
#include "mmap_image.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

bool MmapImage::create(const std::filesystem::path& folder, uint32_t width, uint32_t height) {
    destroy();
    width_ = width;
    height_ = height;

    path_ = newScratchPath(folder);

    int fd = ::open(path_.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) { width_ = height_ = 0; path_.clear(); return false; }
    fd_ = fd;
    ::unlink(path_.c_str());

    // Reserve every block now. A sparse file (plain ftruncate) would let the
    // disk fill up mid-render, and writes through the mapping would then
    // raise SIGBUS and kill the process.
    const uint64_t size = sizeBytes();
    if (posix_fallocate(fd, 0, (off_t)size) != 0) { destroy(); return false; }

    void* p = ::mmap(nullptr, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { destroy(); return false; }
    base_ = (uint8_t*)p;
    return true;
}

void MmapImage::destroy() {
    if (base_) { ::munmap(base_, (size_t)sizeBytes()); base_ = nullptr; }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    // The file was unlinked at creation; closing the descriptor frees it.
    width_ = height_ = 0;
    path_.clear();
}
