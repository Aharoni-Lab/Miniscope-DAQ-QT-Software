// Tiny launcher so the distribution folder shows just one loose file - the
// executable - at the top, with all the DLLs / Qt plugins / QML tucked into a
// "bin" subfolder next to it:
//
//   <dist>/MiniscopeDAQ.exe          <- this launcher (double-click)
//   <dist>/deviceConfigs/ userConfigs/ Scripts/   <- runtime configs (top level)
//   <dist>/bin/MiniscopeDAQ.exe + all DLLs, plugins/, qml/
//
// It launches bin\MiniscopeDAQ.exe with the working directory set to <dist> (the
// launcher's own folder), so the real exe finds:
//   * its DLLs   - in bin\ (Windows searches the exe's own directory first), and
//   * its Qt plugins / QML - via applicationDirPath() == bin\ (see main.cpp), and
//   * its configs - the app reads ./deviceConfigs, ./userConfigs, ./Scripts from
//                   the working directory, which we point at <dist> (top level).
// Self-contained: depends only on kernel32/user32 (always present).

#include <windows.h>
#include <string>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir(buf, n);
    dir.resize(dir.find_last_of(L"\\/"));            // strip "\MiniscopeDAQ.exe" -> <dist>

    std::wstring exe = dir + L"\\bin\\MiniscopeDAQ.exe";
    std::wstring cmd = L"\"" + exe + L"\"";

    STARTUPINFOW si{};  si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // Working directory = <dist> (this launcher's folder), so the app's relative
    // ./deviceConfigs, ./userConfigs and ./Scripts resolve to the top level.
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        0, nullptr, dir.c_str(), &si, &pi)) {
        MessageBoxW(nullptr,
                    (L"Could not start the application:\n" + exe).c_str(),
                    L"Miniscope DAQ", MB_ICONERROR | MB_OK);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
