#include "platform.hpp"
#include <shlobj.h>
#include <mutex>

namespace splitter {
std::filesystem::path dataDirectory() {
    wchar_t* raw = nullptr;
    check(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw), "App-Datenordner öffnen");
    std::unique_ptr<wchar_t, CoMemory> memory(raw);
    auto path = std::filesystem::path(raw) / L"BtAudioSplitter";
    std::filesystem::create_directories(path);
    return path;
}
std::string readFile(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Datei konnte nicht geöffnet werden.");
    input.seekg(0, std::ios::end);
    auto size = input.tellg();
    if (size < 0 || static_cast<unsigned long long>(size) > limit) throw std::runtime_error("Unerwartete Dateigröße.");
    input.seekg(0);
    std::string value(static_cast<std::size_t>(size), '\0');
    if (!input.read(value.data(), static_cast<std::streamsize>(value.size()))) throw std::runtime_error("Datei konnte nicht gelesen werden.");
    return value;
}
void atomicWrite(const std::filesystem::path& path, const std::string& text) {
    auto temp = path; temp += L".tmp";
    {
        Handle file(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.get() == INVALID_HANDLE_VALUE) throw std::runtime_error("Einstellungen konnten nicht geschrieben werden.");
        DWORD written = 0;
        if (!WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size() || !FlushFileBuffers(file.get()))
            throw std::runtime_error("Einstellungen konnten nicht gesichert werden.");
    }
    // Same-volume replacement preserves the old complete file if writing or flushing fails.
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Einstellungsdatei konnte nicht ersetzt werden.");
}
Settings loadSettings(std::string& warning) {
    const auto path = dataDirectory() / L"settings.txt";
    if (!std::filesystem::exists(path)) return {};
    try { auto s = deserialize(readFile(path)); wide(s.source); for (const auto& o : s.outputs) { wide(o.id); wide(o.name); } return s; }
    catch (const std::exception&) {
        auto backup = path; backup += L".invalid-" + std::to_wstring(GetTickCount64());
        if (!MoveFileW(path.c_str(), backup.c_str())) throw std::runtime_error("Beschädigte Einstellungen konnten nicht gesichert werden.");
        warning = "Beschädigte Einstellungen wurden gesichert. Bitte Geräte neu hinzufügen.";
        return {};
    }
}
void logError(const std::string& message) noexcept {
    try {
        static std::mutex lock;
        std::lock_guard guard(lock);
        const auto path = dataDirectory() / L"app.log";
        if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 1000000) {
            auto previous = path; previous += L".previous";
            MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        SYSTEMTIME time{}; GetLocalTime(&time);
        std::ofstream output(path, std::ios::app);
        output << time.wYear << '-' << time.wMonth << '-' << time.wDay << ' ' << time.wHour << ':' << time.wMinute << ':' << time.wSecond << ' ' << message << '\n';
    } catch (...) { /* Diagnostics must never bring down audio or overwrite settings. */ }
}
std::wstring executablePath() {
    std::vector<wchar_t> path(32768);
    auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("Programmpfad konnte nicht gelesen werden.");
    return {path.data(), length};
}
std::wstring quoteArgument(const std::wstring& value) {
    std::wstring result = L"\""; unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') result.append(slashes * 2 + 1, L'\\'); else result.append(slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L'\"';
}
} // namespace splitter
