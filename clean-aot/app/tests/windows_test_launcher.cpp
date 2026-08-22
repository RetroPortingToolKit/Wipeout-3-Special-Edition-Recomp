#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: windows_test_launcher TEST_EXE DLL_DIRECTORY\n";
        return 2;
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                 SEM_NOOPENFILEERRORBOX);

    const DWORD path_size = GetEnvironmentVariableA("PATH", nullptr, 0U);
    std::string path(static_cast<std::size_t>(path_size), '\0');
    if (path_size != 0U) {
        const DWORD copied = GetEnvironmentVariableA(
            "PATH", path.data(), path_size);
        if (copied == 0U || copied >= path_size) return 2;
        path.resize(copied);
    }
    const std::string child_path = std::string(argv[2]) + ";" + path;
    if (SetEnvironmentVariableA("PATH", child_path.c_str()) == FALSE) return 2;

    std::string command = "\"" + std::string(argv[1]) + "\"";
    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessA(argv[1], mutable_command.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                       &process) == FALSE) {
        std::cerr << "unable to launch SDL test without interactive dialogs\n";
        return 2;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 2U;
    const BOOL queried = GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return queried != FALSE ? static_cast<int>(exit_code) : 2;
}
