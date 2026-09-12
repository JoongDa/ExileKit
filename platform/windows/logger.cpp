#include "logger.h"
#include <windows.h>
#include <fstream>
#include <shlobj.h>
#include <string>

namespace poetoolbox {
void Logger::Initialize(std::filesystem::path directory) noexcept {
    try {
        path_ = directory / L"toolbox.log";
    } catch (const std::exception &) {
        OutputDebugStringW(L"POE Toolbox: cannot initialize file logging.\n");
    }
}

void Logger::Write(LogLevel level, std::wstring_view message) const noexcept {
#ifndef _DEBUG
    if (level == LogLevel::Debug || level == LogLevel::Info)
        return;
#endif
    try {
        const wchar_t *labels[] = {L"DEBUG", L"INFO", L"WARN", L"ERROR"};
        std::wstring line = L"[" + std::wstring(labels[static_cast<int>(level)]) + L"] ";
        line.append(message).append(L"\r\n");
        OutputDebugStringW(line.c_str());
        if (path_.empty())
            return;
        std::error_code error;
        if (std::filesystem::file_size(path_, error) >= 1024 * 1024 && !error) {
            auto backup = path_;
            backup += L".1";
            std::filesystem::remove(backup, error);
            error.clear();
            std::filesystem::rename(path_, backup, error);
            if (error)
                return;
        }
        const int count =
            WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8.data(), count, nullptr,
                            nullptr);
        std::ofstream file(path_, std::ios::binary | std::ios::app);
        file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    } catch (const std::exception &) {
        OutputDebugStringW(L"POE Toolbox: log write failed.\n");
    }
}
} // namespace poetoolbox
