#include "ui.hpp"
#include "audio.hpp"
#include "../artifacts/native/version.h"
#include <shellapi.h>
#include <exception>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace splitter;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ComApartment apartment(COINIT_APARTMENTTHREADED);
        int count = 0; auto** args = CommandLineToArgvW(GetCommandLineW(), &count);
        struct Free { wchar_t** p; ~Free() { LocalFree(p); } } free{args};
        if (!args) throw std::runtime_error("Argumente konnten nicht gelesen werden.");
        if (count == 3 && std::wstring(args[1]) == L"--rename-cable") { renameCable(utf8(args[2])); return 0; }
        if (count == 3 && std::wstring(args[1]) == L"--preview") return runUi(instance, args[2]);
        if (count == 3 && std::wstring(args[1]) == L"--preview-missing-driver") return runUi(instance, args[2], true);
        if (count == 3 && std::wstring(args[1]) == L"--diagnose") {
            std::string report;
            for (const auto& d : enumerateOutputs()) report += d.name + "\t" + (d.available ? "active" : "offline") + "\t" + (d.cable ? "cable" : "output") + "\t" + d.id + "\n";
            atomicWrite(args[2], report); return 0;
        }
        if (count == 3 && std::wstring(args[1]) == L"--audio-self-test") {
            std::exception_ptr error;
            std::thread test([&] { try { audioSelfTest(args[2]); } catch (...) { error = std::current_exception(); } }); test.join();
            if (error) std::rethrow_exception(error); return 0;
        }
        if (count != 1) throw std::runtime_error("Unbekannte Programmargumente.");
        Handle mutex(CreateMutexW(nullptr, FALSE, L"Local\\MarcusBeckerle.BtAudioSplitter"));
        if (!mutex.get()) throw std::runtime_error("Instanzsperre konnte nicht erstellt werden.");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (auto other = FindWindowW(L"BtAudioSplitterNative", nullptr)) {
                ShowWindow(other, SW_RESTORE); SetForegroundWindow(other);
                wchar_t title[256]{}; GetWindowTextW(other, title, 256);
                if (std::wstring(title) != L"BT Audio Splitter · " + wide(APP_VERSION_TEXT)) {
                    auto message = L"Es läuft bereits eine andere Version von Audio Splitter.\n\nBitte das alte Fenster schließen und diese EXE erneut starten, um Version " + wide(APP_VERSION_TEXT) + L" zu verwenden.\n\nDie laufende Audioausgabe wird nicht automatisch beendet.";
                    MessageBoxW(other, message.c_str(), L"Neue Version starten", MB_OK | MB_ICONINFORMATION);
                }
            }
            return 0;
        }
        return runUi(instance);
    } catch (const std::exception& e) {
        logError(e.what()); MessageBoxW(nullptr, wide(e.what()).c_str(), L"Audio Splitter", MB_OK | MB_ICONERROR); return 1;
    }
}
