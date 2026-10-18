#include "setup.hpp"
#include "../artifacts/native/version.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <shellapi.h>
#include <array>

namespace splitter {
namespace {
struct Internet {
    HINTERNET handle;
    explicit Internet(HINTERNET h) : handle(h) { if (!h) throw std::runtime_error("Treiber-Download konnte nicht geöffnet werden."); }
    ~Internet() { WinHttpCloseHandle(handle); }
    Internet(const Internet&) = delete;
};
std::string download() {
    Internet session(WinHttpOpen(wide("BT-Audio-Splitter/" APP_VERSION_TEXT).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    WinHttpSetTimeouts(session.handle, 15000, 15000, 30000, 30000);
    Internet server(WinHttpConnect(session.handle, L"download.vb-audio.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    Internet request(WinHttpOpenRequest(server.handle, L"GET", L"/Download_CABLE/VBCABLE_Driver_Pack45.zip", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.handle, nullptr))
        throw std::runtime_error("Treiber-Download fehlgeschlagen. Internetverbindung prüfen.");
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX) || status != 200)
        throw std::runtime_error("Hersteller-Server hat das Treiberpaket nicht geliefert.");
    std::string data; std::array<char, 65536> buffer{}; DWORD read = 0;
    auto started = GetTickCount64();
    while (true) {
        if (!WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) throw std::runtime_error("Treiber-Download unterbrochen.");
        if (!read) break;
        if (data.size() + read > 8000000 || GetTickCount64() - started > 90000) throw std::runtime_error("Unerwartete Download-Größe oder Zeitüberschreitung.");
        data.append(buffer.data(), read);
    }
    return data;
}
std::string sha256(const std::string& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("SHA-256 nicht verfügbar.");
    struct Close { BCRYPT_ALG_HANDLE h; ~Close() { BCryptCloseAlgorithmProvider(h, 0); } } close{algorithm};
    std::array<UCHAR, 32> hash{};
    if (BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()), hash.data(), static_cast<ULONG>(hash.size())) < 0)
        throw std::runtime_error("Prüfsumme konnte nicht berechnet werden.");
    constexpr char hex[] = "0123456789abcdef";
    std::string result; for (auto c : hash) { result += hex[c >> 4]; result += hex[c & 15]; } return result;
}
std::wstring psLiteral(const std::wstring& value) {
    std::wstring result = L"'";
    for (auto c : value) { result += c; if (c == L'\'') result += c; }
    return result + L"'";
}
std::wstring base64(const std::wstring& command) {
    constexpr wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = reinterpret_cast<const unsigned char*>(command.data());
    std::size_t count = command.size() * sizeof(wchar_t); std::wstring result;
    for (std::size_t i = 0; i < count; i += 3) {
        unsigned n = bytes[i] << 16;
        if (i + 1 < count) n |= bytes[i + 1] << 8;
        if (i + 2 < count) n |= bytes[i + 2];
        result += alphabet[(n >> 18) & 63]; result += alphabet[(n >> 12) & 63];
        result += i + 1 < count ? alphabet[(n >> 6) & 63] : L'=';
        result += i + 2 < count ? alphabet[n & 63] : L'=';
    }
    return result;
}
void extract(const std::filesystem::path& archive, const std::filesystem::path& directory) {
    wchar_t system[MAX_PATH]; if (!GetSystemDirectoryW(system, MAX_PATH)) throw std::runtime_error("Windows-Verzeichnis nicht gefunden.");
    auto powershell = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    auto script = L"$ErrorActionPreference='Stop'; try { Expand-Archive -LiteralPath " + psLiteral(archive.wstring()) + L" -DestinationPath " + psLiteral(directory.wstring()) + L"; exit 0 } catch { exit 1 }";
    auto args = quoteArgument(powershell.wstring()) + L" -NoLogo -NoProfile -NonInteractive -EncodedCommand " + base64(script);
    STARTUPINFOW start{}; start.cb = sizeof(start); PROCESS_INFORMATION info{};
    if (!CreateProcessW(powershell.c_str(), args.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &start, &info))
        throw std::runtime_error("Windows-ZIP-Entpacker konnte nicht gestartet werden.");
    Handle process(info.hProcess), thread(info.hThread);
    if (WaitForSingleObject(process.get(), 60000) != WAIT_OBJECT_0) {
        TerminateProcess(process.get(), 1); WaitForSingleObject(process.get(), INFINITE);
        throw std::runtime_error("Entpacken des Treibers dauerte zu lange.");
    }
    DWORD code = 1; GetExitCodeProcess(process.get(), &code);
    if (code) throw std::runtime_error("Treiber konnte nicht entpackt werden. Windows PowerShell muss verfügbar sein.");
}
void verifySignature(const std::filesystem::path& file) {
    WINTRUST_FILE_INFO info{}; info.cbStruct = sizeof(info); info.pcwszFilePath = file.c_str();
    WINTRUST_DATA data{}; data.cbStruct = sizeof(data); data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE; data.dwUnionChoice = WTD_CHOICE_FILE; data.pFile = &info;
    data.dwStateAction = WTD_STATEACTION_VERIFY; data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    auto result = WinVerifyTrust(nullptr, &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr, &action, &data);
    if (result != ERROR_SUCCESS) throw std::runtime_error("Signatur des VB-CABLE-Setups konnte nicht bestätigt werden.");
}
void elevated(HWND owner, const std::wstring& exe, const std::wstring& args, const std::wstring& directory = {}) {
    SHELLEXECUTEINFOW info{}; info.cbSize = sizeof(info); info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.hwnd = owner; info.lpVerb = L"runas"; info.lpFile = exe.c_str(); info.lpParameters = args.c_str();
    info.lpDirectory = directory.empty() ? nullptr : directory.c_str(); info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) throw std::runtime_error("Administratorfreigabe abgebrochen oder Programm konnte nicht gestartet werden.");
    Handle process(info.hProcess);
    if (!process.get()) throw std::runtime_error("Setup-Prozess konnte nicht verfolgt werden.");
    WaitForSingleObject(process.get(), INFINITE);
    DWORD code = 1; GetExitCodeProcess(process.get(), &code);
    if (code != 0 && code != 3010) throw std::runtime_error("Setup/Hilfsprogramm meldet Fehler " + std::to_string(code) + ".");
}
} // namespace
void runDriverSetup(const std::function<void(const std::wstring&)>& progress) {
    ComApartment apartment(COINIT_APARTMENTTHREADED);
    progress(L"VB-CABLE wird vom Hersteller geladen …");
    auto data = download();
    if (sha256(data) != "b950e39f01af1d04ea623c8f6d8eb9b6ea5c477c637295fabf20631c85116bfb")
        throw std::runtime_error("Treiber-Prüfsumme stimmt nicht überein. Bitte App aktualisieren.");
    GUID guid{}; check(CoCreateGuid(&guid), "Setup-Verzeichnis erzeugen");
    wchar_t id[40]; StringFromGUID2(guid, id, 40);
    auto directory = dataDirectory() / (std::wstring(L"setup-") + id);
    std::filesystem::create_directory(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } cleanup{directory};
    auto archive = directory / L"driver.zip", extraction = directory / L"package";
    atomicWrite(archive, data); progress(L"Geprüftes Treiberpaket wird entpackt …");
    extract(archive, extraction);
    auto setup = extraction / L"VBCABLE_Setup_x64.exe"; verifySignature(setup);
    progress(L"Im VB-CABLE-Fenster „Install Driver“ oder „Remove Driver“ wählen.");
    elevated(nullptr, setup.wstring(), L"", extraction.wstring());
    progress(L"Setup geschlossen. Nach Installation/Entfernung Windows neu starten.");
}
void runRenameHelper(HWND owner, const std::string& source) {
    ComApartment apartment(COINIT_APARTMENTTHREADED);
    elevated(owner, executablePath(), L"--rename-cable " + quoteArgument(wide(source)));
}
void openLink(HWND owner, const wchar_t* url) {
    auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", url, nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) throw std::runtime_error("Windows konnte den Link nicht öffnen.");
}
} // namespace splitter
