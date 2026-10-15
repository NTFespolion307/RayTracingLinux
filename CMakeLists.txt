cmake_minimum_required(VERSION 3.24)
project(VkGigaTracer LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Statically link the MSVC runtime so the exe needs no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Keep FetchContent downloads in a shared cache so reconfigures don't re-download.
if(NOT DEFINED FETCHCONTENT_BASE_DIR)
    set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/_deps_cache" CACHE PATH "")
endif()

# All Vulkan entry points come from volk, never from static prototypes.
add_compile_definitions(VK_NO_PROTOTYPES)

if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
                            UNICODE _UNICODE VK_USE_PLATFORM_WIN32_KHR)
endif()

if(MSVC)
    add_compile_options(/MP /bigobj /utf-8 /EHsc /wd4244 /wd4267 /wd4305)
endif()

# Linux: GLFW is built for X11 only for now (Wayland support is planned later).
if(UNIX AND NOT APPLE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11     ON  CACHE BOOL "" FORCE)
endif()

find_package(Threads REQUIRED)

include(FetchContent)

# ---------------------------------------------------------------------------
# Dependencies (all pinned, built from source, statically linked)
# ---------------------------------------------------------------------------

# --- Vulkan-Headers (provides Vulkan::Headers target) ---
FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.3.296
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(VulkanHeaders)

# --- volk (dynamic Vulkan loader; picks up Vulkan::Headers) ---
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.3.296.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(volk)

# --- Vulkan Memory Allocator (header only; we compile the impl ourselves) ---
FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE)
FetchContent_GetProperties(vma)
if(NOT vma_POPULATED)
    FetchContent_Populate(vma)
endif()

# --- GLFW (static) ---
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glfw)

# --- Dear ImGui (no CMake of its own; we build the sources we need) ---
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

# --- tinyobjloader (single header) ---
FetchContent_Declare(tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG        v1.0.6
    GIT_SHALLOW    TRUE)
FetchContent_GetProperties(tinyobjloader)
if(NOT tinyobjloader_POPULATED)
    FetchContent_Populate(tinyobjloader)
endif()

# --- zlib (static) ---
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(zlib)
# zlib's CMake creates zlibstatic; expose include dirs (source + generated zconf.h).
set(ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")

# --- libpng: we compile the sources ourselves to avoid its find_package(ZLIB) ---
FetchContent_Declare(libpng
    GIT_REPOSITORY https://github.com/pnggroup/libpng.git
    GIT_TAG        v1.6.44
    GIT_SHALLOW    TRUE)
FetchContent_GetProperties(libpng)
if(NOT libpng_POPULATED)
    FetchContent_Populate(libpng)
endif()
# Provide pnglibconf.h from the shipped prebuilt config.
configure_file(${libpng_SOURCE_DIR}/scripts/pnglibconf.h.prebuilt
               ${libpng_BINARY_DIR}/pnglibconf.h COPYONLY)
add_library(png_static STATIC
    ${libpng_SOURCE_DIR}/png.c
    ${libpng_SOURCE_DIR}/pngerror.c
    ${libpng_SOURCE_DIR}/pngget.c
    ${libpng_SOURCE_DIR}/pngmem.c
    ${libpng_SOURCE_DIR}/pngpread.c
    ${libpng_SOURCE_DIR}/pngread.c
    ${libpng_SOURCE_DIR}/pngrio.c
    ${libpng_SOURCE_DIR}/pngrtran.c
    ${libpng_SOURCE_DIR}/pngrutil.c
    ${libpng_SOURCE_DIR}/pngset.c
    ${libpng_SOURCE_DIR}/pngtrans.c
    ${libpng_SOURCE_DIR}/pngwio.c
    ${libpng_SOURCE_DIR}/pngwrite.c
    ${libpng_SOURCE_DIR}/pngwtran.c
    ${libpng_SOURCE_DIR}/pngwutil.c)
target_compile_definitions(png_static PRIVATE PNG_INTEL_SSE_OPT=0 PNG_ARM_NEON_OPT=0)
target_include_directories(png_static PUBLIC
    ${libpng_SOURCE_DIR} ${libpng_BINARY_DIR} ${ZLIB_INCLUDE_DIRS})
target_link_libraries(png_static PUBLIC zlibstatic)

# --- libjpeg-turbo (static, SIMD off so no NASM needed) ---
# libjpeg-turbo forbids add_subdirectory() integration, so we build it as an
# isolated ExternalProject and link the resulting static library.
FetchContent_Declare(libjpegturbo
    GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
    GIT_TAG        3.0.4
    GIT_SHALLOW    TRUE)
FetchContent_GetProperties(libjpegturbo)
if(NOT libjpegturbo_POPULATED)
    FetchContent_Populate(libjpegturbo)
endif()

include(ExternalProject)
set(JPEG_BUILD_DIR "${CMAKE_BINARY_DIR}/jpegturbo-build")

# libjpeg-turbo names its static library jpeg-static.lib under MSVC and
# libjpeg.a elsewhere. Multi-config generators (Visual Studio) put it in a
# per-config subfolder; single-config ones (Ninja/Make) don't.
get_property(VKGT_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(MSVC)
    set(JPEG_LIB_NAME "jpeg-static.lib")
else()
    set(JPEG_LIB_NAME "libjpeg.a")
endif()
if(VKGT_MULTI_CONFIG)
    set(JPEG_LIB_BYPRODUCT "${JPEG_BUILD_DIR}/$<CONFIG>/${JPEG_LIB_NAME}")
    set(JPEG_EXTRA_ARGS "")
else()
    set(JPEG_LIB_BYPRODUCT "${JPEG_BUILD_DIR}/${JPEG_LIB_NAME}")
    set(JPEG_EXTRA_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
endif()

ExternalProject_Add(jpegturbo_ext
    SOURCE_DIR       "${libjpegturbo_SOURCE_DIR}"
    BINARY_DIR       "${JPEG_BUILD_DIR}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND   ""
    CMAKE_ARGS
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DWITH_SIMD=OFF
        -DWITH_TURBOJPEG=OFF
        -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        ${JPEG_EXTRA_ARGS}
    BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target jpeg-static
    INSTALL_COMMAND  ""
    BUILD_BYPRODUCTS "${JPEG_LIB_BYPRODUCT}")

add_library(jpegturbo STATIC IMPORTED GLOBAL)
set_target_properties(jpegturbo PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${libjpegturbo_SOURCE_DIR};${JPEG_BUILD_DIR}")
if(VKGT_MULTI_CONFIG)
    # Per-config imported locations (generator expressions are not expanded
    # inside IMPORTED_LOCATION, so set each config explicitly).
    set_target_properties(jpegturbo PROPERTIES
        IMPORTED_CONFIGURATIONS "RELEASE;DEBUG;RELWITHDEBINFO;MINSIZEREL"
        IMPORTED_LOCATION_RELEASE        "${JPEG_BUILD_DIR}/Release/${JPEG_LIB_NAME}"
        IMPORTED_LOCATION_DEBUG          "${JPEG_BUILD_DIR}/Debug/${JPEG_LIB_NAME}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${JPEG_BUILD_DIR}/RelWithDebInfo/${JPEG_LIB_NAME}"
        IMPORTED_LOCATION_MINSIZEREL     "${JPEG_BUILD_DIR}/MinSizeRel/${JPEG_LIB_NAME}"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RELWITHDEBINFO;RELEASE"
        MAP_IMPORTED_CONFIG_MINSIZEREL     "MINSIZEREL;RELEASE")
else()
    set_target_properties(jpegturbo PROPERTIES
        IMPORTED_LOCATION "${JPEG_BUILD_DIR}/${JPEG_LIB_NAME}")
endif()

# --- Python (build-time only; glslang's code generators need it) ---
if(WIN32)
    # Fetched automatically so the build machine needs nothing but VS, CMake, net.
    FetchContent_Declare(pythonembed
        URL https://www.python.org/ftp/python/3.12.7/python-3.12.7-embed-amd64.zip)
    FetchContent_GetProperties(pythonembed)
    if(NOT pythonembed_POPULATED)
        FetchContent_Populate(pythonembed)
    endif()
    set(Python3_EXECUTABLE "${pythonembed_SOURCE_DIR}/python.exe" CACHE FILEPATH "" FORCE)
    set(Python_EXECUTABLE  "${pythonembed_SOURCE_DIR}/python.exe" CACHE FILEPATH "" FORCE)
else()
    # Linux distributions ship python3; use it.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

# --- glslang (host shader compiler; no SPIRV-Tools optimizer to keep it lean) ---
set(ENABLE_OPT              OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON  CACHE BOOL "" FORCE)
set(GLSLANG_TESTS          OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL         OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG        14.3.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glslang)

# ---------------------------------------------------------------------------
# Dear ImGui static library (core + GLFW + Vulkan backends)
# ---------------------------------------------------------------------------
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw Vulkan::Headers)
# The Vulkan backend must not pull in static prototypes; it uses a loader we feed.
target_compile_definitions(imgui PUBLIC VK_NO_PROTOTYPES IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

# ---------------------------------------------------------------------------
# Shader compilation: GLSL -> SPIR-V (two variants) -> embedded C headers
# ---------------------------------------------------------------------------
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GEN_DIR})

set(SHADER_SRC   "${CMAKE_SOURCE_DIR}/shaders/pathtrace.comp")
set(SHADER_COMMON "${CMAKE_SOURCE_DIR}/shaders/common.glsl")

# The standalone glslang front-end executable target.
set(GLSLANG_EXE glslang-standalone)

function(compile_and_embed variant_name target_env defines)
    set(spv "${GEN_DIR}/${variant_name}.spv")
    set(hdr "${GEN_DIR}/${variant_name}.h")
    add_custom_command(
        OUTPUT ${spv}
        COMMAND ${GLSLANG_EXE} -V --target-env ${target_env} ${defines}
                -S comp "${SHADER_SRC}" -o "${spv}"
        DEPENDS ${SHADER_SRC} ${SHADER_COMMON} ${GLSLANG_EXE}
        COMMENT "Compiling ${variant_name}.spv"
        VERBATIM)
    add_custom_command(
        OUTPUT ${hdr}
        COMMAND ${CMAKE_COMMAND} -DINPUT=${spv} -DOUTPUT=${hdr}
                -DVARNAME=${variant_name}_spv
                -P "${CMAKE_SOURCE_DIR}/cmake/embed_spirv.cmake"
        DEPENDS ${spv} "${CMAKE_SOURCE_DIR}/cmake/embed_spirv.cmake"
        COMMENT "Embedding ${variant_name}.spv"
        VERBATIM)
endfunction()

compile_and_embed(pathtrace_compute  vulkan1.1 "-DUSE_RAY_QUERY=0")
compile_and_embed(pathtrace_rayquery vulkan1.2 "-DUSE_RAY_QUERY=1")

add_custom_target(shaders DEPENDS
    ${GEN_DIR}/pathtrace_compute.h
    ${GEN_DIR}/pathtrace_rayquery.h)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
set(APP_SOURCES
    src/main.cpp
    src/vk_context.cpp
    src/vma_impl.cpp
    src/mmap_image.cpp
    src/scene.cpp
    src/obj_loader.cpp
    src/bvh.cpp
    src/accel.cpp
    src/renderer.cpp
    src/viewer.cpp
    src/savers.cpp
    src/theme.cpp
    src/ui.cpp)

add_executable(VkGigaTracer WIN32 ${APP_SOURCES})
add_dependencies(VkGigaTracer shaders jpegturbo_ext)

target_include_directories(VkGigaTracer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GEN_DIR}
    ${vma_SOURCE_DIR}/include
    ${tinyobjloader_SOURCE_DIR}
    ${ZLIB_INCLUDE_DIRS})

target_link_libraries(VkGigaTracer PRIVATE
    volk
    Vulkan::Headers
    glfw
    imgui
    png_static
    zlibstatic
    jpegturbo
    Threads::Threads)

if(WIN32)
    target_link_libraries(VkGigaTracer PRIVATE Ole32 Shell32)  # COM file dialogs
else()
    # volk loads libvulkan.so.1 with dlopen. Static libstdc++/libgcc keep the
    # binary "copy and run" like the Windows exe (only glibc is required).
    target_link_libraries(VkGigaTracer PRIVATE ${CMAKE_DL_LIBS})
    target_link_options(VkGigaTracer PRIVATE -static-libstdc++ -static-libgcc)
endif()

set_target_properties(VkGigaTracer PROPERTIES OUTPUT_NAME "VkGigaTracer")

# ---------------------------------------------------------------------------
# Bundle UI fonts (Roboto) next to the executable.
# ---------------------------------------------------------------------------
# The Material UI loads Roboto-Regular.ttf / Roboto-Medium.ttf from a "fonts"
# folder beside the .exe (see theme::loadFonts). Copy the repo's fonts/ dir
# after every build. If the TTFs are absent the app falls back to a system
# font, so this step is harmless when the folder only holds the README.
add_custom_command(TARGET VkGigaTracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/fonts"
            "$<TARGET_FILE_DIR:VkGigaTracer>/fonts"
    COMMENT "Copying bundled UI fonts")

# ---------------------------------------------------------------------------
# Tests (doctest unit tests + golden-image self-test). Run with:
#   ctest --test-dir build -C Release --output-on-failure
# Tests labelled "gpu" need a Vulkan device; skip them with -LE gpu.
# ---------------------------------------------------------------------------
option(VKGT_BUILD_TESTS "Build the VkGigaTracer test suite" ON)
if(VKGT_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE)
    FetchContent_GetProperties(doctest)
    if(NOT doctest_POPULATED)
        FetchContent_Populate(doctest)
    endif()

    # CPU-only unit tests: compile the app sources that don't need Vulkan.
    add_executable(VkGigaTracerTests
        tests/test_main.cpp
        tests/test_render_settings.cpp
        tests/test_mmap_image.cpp
        tests/test_savers.cpp
        tests/test_obj_loader.cpp
        tests/test_scene.cpp
        tests/test_bvh.cpp
        tests/test_gpu_layout.cpp
        src/mmap_image.cpp
        src/savers.cpp
        src/obj_loader.cpp
        src/scene.cpp
        src/bvh.cpp)
    add_dependencies(VkGigaTracerTests jpegturbo_ext)
    target_include_directories(VkGigaTracerTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${doctest_SOURCE_DIR}
        ${tinyobjloader_SOURCE_DIR}
        ${ZLIB_INCLUDE_DIRS})
    target_compile_definitions(VkGigaTracerTests PRIVATE
        VKGT_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
        DOCTEST_CONFIG_USE_STD_HEADERS)  # avoid std:: forward decls (MSVC C5285)
    target_link_libraries(VkGigaTracerTests PRIVATE png_static zlibstatic jpegturbo)

    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
    doctest_discover_tests(VkGigaTracerTests)

    # Golden-image comparison tool for the --selftest render.
    add_executable(ppm_compare tests/tools/ppm_compare.cpp)
    target_link_libraries(ppm_compare PRIVATE png_static zlibstatic)

    set(SELFTEST_DIR "${CMAKE_BINARY_DIR}/selftest_run")
    file(MAKE_DIRECTORY ${SELFTEST_DIR})
    add_test(NAME selftest_render
             COMMAND VkGigaTracer --selftest
             WORKING_DIRECTORY ${SELFTEST_DIR})
    add_test(NAME selftest_golden
             COMMAND ppm_compare ${SELFTEST_DIR}/selftest_out.ppm
                     ${CMAKE_SOURCE_DIR}/tests/golden/selftest_1080p_16spp.png)
    set_tests_properties(selftest_render PROPERTIES
        LABELS gpu TIMEOUT 600 FIXTURES_SETUP selftest)
    set_tests_properties(selftest_golden PROPERTIES
        LABELS gpu FIXTURES_REQUIRED selftest)
endif()
