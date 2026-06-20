// Tiny launcher so the distribution folder shows just one exe at the top while
// all the DLLs / plugins / QML live in a "bin" subfolder next to it.
//
//   <dist>/MiniscopeDAQ.exe   <- this launcher
//   <dist>/bin/MiniscopeDAQ.exe + all DLLs, plugins/, qml/, deviceConfigs/, ...
//
// It launches bin\MiniscopeDAQ.exe with the working directory set to bin\, so
// the real exe finds its DLLs (same folder) and its deviceConfigs/ (cwd).
// Self-contained: depends only on kernel32/user32 (always present).

#include <windows.h>
#include <string>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir(buf, n);
    dir.resize(dir.find_last_of(L"\\/"));            // strip "\MiniscopeDAQ.exe"

    std::wstring binDir = dir + L"\\bin";
    std::wstring exe    = binDir + L"\\MiniscopeDAQ.exe";
    std::wstring cmd    = L"\"" + exe + L"\"";

    STARTUPINFOW si{};  si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        0, nullptr, binDir.c_str(), &si, &pi)) {
        MessageBoxW(nullptr,
                    (L"Could not start the application:\n" + exe).c_str(),
                    L"Miniscope DAQ", MB_ICONERROR | MB_OK);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
