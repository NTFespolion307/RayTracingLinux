// test_util.h - small helpers shared by the unit tests: per-test scratch
// directories and FILE* opening that works for non-ASCII paths on Windows.
#pragma once
#include "platform.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace testutil {

namespace fs = std::filesystem;

// Creates a fresh, empty directory under the system temp dir and removes it
// (with its contents) when the object goes out of scope.
class TempDir {
public:
    explicit TempDir(const std::string& tag) {
        static int counter = 0;
        path_ = fs::temp_directory_path() /
                ("vkgt_test_" + tag + "_" + std::to_string(++counter));
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

inline FILE* openFile(const fs::path& p, const char* mode) {
    return platform::openFile(p, mode);
}

} // namespace testutil

#ifndef VKGT_TEST_DATA_DIR
#error "VKGT_TEST_DATA_DIR must point at tests/data"
#endif
