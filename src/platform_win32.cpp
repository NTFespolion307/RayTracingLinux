// platform_win32.cpp - Windows implementation of platform.h.
#include "platform.h"
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

namespace platform {

namespace {
bool g_comReady = false;

std::wstring utf8ToWide(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

// Runs a configured dialog and returns the chosen file-system path.
std::filesystem::path showDialog(IFileDialog* dlg) {
    std::filesystem::path result;
    if (SUCCEEDED(dlg->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    return result;
}
} // namespace

void initProcess() {
    // Per-Monitor-V2 DPI awareness so the UI is crisp on high-DPI displays.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // COM for IFileDialog (open/save/folder pickers).
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    g_comReady = SUCCEEDED(hr);
}

void shutdownProcess() {
    if (g_comReady) CoUninitialize();
    g_comReady = false;
}

void showMessage(MessageKind kind, const char* text, const char* title) {
    UINT icon = (kind == MessageKind::Error) ? MB_ICONERROR : MB_ICONWARNING;
    MessageBoxW(nullptr, utf8ToWide(text).c_str(), utf8ToWide(title).c_str(), MB_OK | icon);
}

std::filesystem::path openFileDialog(const std::vector<FileFilter>& filters) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg)))) return {};
    std::vector<std::wstring> strings;
    strings.reserve(filters.size() * 2);
    std::vector<COMDLG_FILTERSPEC> specs;
    for (const FileFilter& f : filters) {
        strings.push_back(utf8ToWide(f.description));
        strings.push_back(utf8ToWide(f.pattern));
    }
    for (size_t i = 0; i < filters.size(); ++i)
        specs.push_back({ strings[i * 2].c_str(), strings[i * 2 + 1].c_str() });
    if (!specs.empty()) dlg->SetFileTypes((UINT)specs.size(), specs.data());
    std::filesystem::path result = showDialog(dlg);
    dlg->Release();
    return result;
}

std::filesystem::path saveFileDialog(const FileFilter& filter, const char* defaultExtension) {
    IFileSaveDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg)))) return {};
    std::wstring desc = utf8ToWide(filter.description);
    std::wstring pattern = utf8ToWide(filter.pattern);
    std::wstring ext = utf8ToWide(defaultExtension);
    COMDLG_FILTERSPEC type[] = { { desc.c_str(), pattern.c_str() } };
    dlg->SetFileTypes(1, type);
    dlg->SetDefaultExtension(ext.c_str());
    std::filesystem::path result = showDialog(dlg);
    dlg->Release();
    return result;
}

std::filesystem::path pickFolderDialog() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg)))) return {};
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS);
    std::filesystem::path result = showDialog(dlg);
    dlg->Release();
    return result;
}

std::filesystem::path exeDir() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); break; }
        buf.resize(buf.size() * 2);   // long path: grow and retry
    }
    return std::filesystem::path(buf).parent_path();
}

unsigned processId() { return (unsigned)GetCurrentProcessId(); }

FILE* openFile(const std::filesystem::path& path, const char* mode) {
    return _wfopen(path.c_str(), utf8ToWide(mode).c_str());
}

bool localTime(std::time_t t, std::tm& out) {
    return localtime_s(&out, &t) == 0;
}

std::filesystem::path systemUiFont(bool semibold) {
    std::filesystem::path p = semibold ? L"C:\\Windows\\Fonts\\seguisb.ttf"   // Segoe UI Semibold
                                       : L"C:\\Windows\\Fonts\\segoeui.ttf";
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec) ? p : std::filesystem::path();
}

} // namespace platform
