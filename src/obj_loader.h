// obj_loader.h - OBJ/MTL import with triangulation, smooth-normal generation,
// deterministic material mapping, auto-centering and camera framing.
#pragma once
#include "scene.h"
#include <filesystem>
#include <string>

struct ObjLoadResult {
    bool        ok = false;
    std::string message;   // error or warning text for the UI
    Scene       scene;
};

// Loads an OBJ (and its MTL files) from any path, including non-ASCII ones.
ObjLoadResult loadObj(const std::filesystem::path& path);
