// Exercise the real Win32 invalidation path. A static WM_PRINT preview cannot
// detect stale pixels left behind when native child windows move during scrolling.
#include "../ui.cpp"
#include <iostream>
#include <chrono>

namespace splitter {
namespace {
struct WindowRenderingTest {
    static bool isSpectrum(HWND control) { int id = GetDlgCtrlID(control); return id == 30 || (id >= 300 && id < 305); }
    static LRESULT CALLBACK observePaint(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        if (msg == WM_DRAWITEM) ++*reinterpret_cast<int*>(data);
        return DefSubclassProc(w, msg, wp, lp);
    }
    static LRESULT CALLBACK observeStaticFrame(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto* counts = reinterpret_cast<int*>(data);
        if (msg == WM_PAINT) ++counts[0];
        if (msg == WM_DRAWITEM) ++counts[1];
        return DefSubclassProc(w, msg, wp, lp);
    }
    static std::vector<DWORD> pixels(HWND window) {
        RECT client{}; GetClientRect(window, &client);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = client.right; info.bmiHeader.biHeight = -client.bottom;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        HDC source = GetDC(window), memory = CreateCompatibleDC(source);
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap) { DeleteDC(memory); ReleaseDC(window, source); throw std::runtime_error("Cannot allocate UI capture."); }
        auto old = SelectObject(memory, bitmap);
        BOOL copied = BitBlt(memory, 0, 0, client.right, client.bottom, source, 0, 0, SRCCOPY);
        GdiFlush();
        const auto* begin = static_cast<DWORD*>(bits);
        std::vector<DWORD> result(begin, begin + static_cast<std::size_t>(client.right) * client.bottom);
        for (auto& pixel : result) pixel &= 0x00ffffff; // GDI's unused alpha byte is unspecified.
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window, source);
        if (!copied) throw std::runtime_error("Cannot capture the test window's client surface.");
        return result;
    }
    static void verify(Window& window, const std::string& scenario, bool flush = true) {
        // Process only the regions invalidated by the application, as the message loop does.
        if (flush) RedrawWindow(window.window_, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
        auto actual = pixels(window.window_);
        // A complete repaint at the same scroll offset is the reference image.
        RedrawWindow(window.window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        auto expected = pixels(window.window_);
        if (std::adjacent_find(expected.begin(), expected.end(), std::not_equal_to<DWORD>()) == expected.end())
            throw std::runtime_error("UI capture is blank; a Windows desktop is required.");
        if (actual != expected) {
            std::size_t differences = 0;
            for (std::size_t i = 0; i < actual.size(); ++i) differences += actual[i] != expected[i];
            throw std::runtime_error(scenario + ": " + std::to_string(differences) + " stale pixels after layout.");
        }
    }
    static void liveButtons(Window& window) {
        window.scroll(window.d(window.setupTop() - 160));
        auto reference = pixels(window.window_);
        RECT client{}; GetClientRect(window.window_, &client);
        std::vector<RECT> buttons;
        for (HWND child = GetWindow(window.window_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (isSpectrum(child)) continue;
            if (!IsWindowVisible(child)) continue;
            RECT area{}, clipped{}; GetWindowRect(child, &area);
            MapWindowPoints(nullptr, window.window_, reinterpret_cast<POINT*>(&area), 2);
            if (IntersectRect(&clipped, &area, &client)) buttons.push_back(clipped);
        }
        using Clock = std::chrono::steady_clock;
        int staticPaints[2]{};
        SetWindowSubclass(window.window_, observeStaticFrame, 100, reinterpret_cast<DWORD_PTR>(staticPaints));
        auto next = Clock::now(), deadline = next + std::chrono::seconds(3); int ticks = 0, samples = 0;
        while (Clock::now() < deadline && (ticks < 30 || GetQueueStatus(QS_PAINT) >> 16)) {
            if (Clock::now() >= next && ticks < 30) {
                window.snapshot_.spectrum.fill((++ticks % 10) / 10.0f);
                PostMessageW(window.window_, WM_TIMER, 1, 0);
                next = Clock::now() + std::chrono::milliseconds(20);
            }
            MSG message{};
            if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
                if (message.message == WM_PAINT || message.message == WM_TIMER) {
                    auto frame = pixels(window.window_); ++samples;
                    for (const auto& area : buttons) for (LONG y = area.top; y < area.bottom; ++y) for (LONG x = area.left; x < area.right; ++x) {
                        auto at = static_cast<std::size_t>(y) * client.right + x;
                        if (frame[at] != reference[at]) throw std::runtime_error("Live meter repaint changes a static button at " + std::to_string(x) + "," + std::to_string(y));
                    }
                }
            } else Sleep(1);
        }
        RemoveWindowSubclass(window.window_, observeStaticFrame, 100);
        if (samples < 30) throw std::runtime_error("Live flicker test did not process enough frames.");
        if (staticPaints[0] || staticPaints[1]) throw std::runtime_error("Live meters repaint static surfaces: parent=" +
            std::to_string(staticPaints[0]) + ", buttons=" + std::to_string(staticPaints[1]));
        std::cout << "PASS live message-loop button stability: " << samples << " intermediate frames" << std::endl;
    }
    static int run(bool liveOnly = false) {
        Window window(true); HWND handle = window.create(); KillTimer(handle, 1);
        // Only this test window is shown/captured. No audio, settings or driver operations run.
        SetWindowPos(handle, HWND_TOPMOST, 10, 10, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(handle, SW_SHOWNOACTIVATE);
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        SetWindowPos(handle, nullptr, work.left + 10, work.top + 10, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        int checks = 0;
        liveButtons(window);
        if (liveOnly) { DestroyWindow(handle); return 0; }
        int immediatePaints = 0;
        SetWindowSubclass(handle, observePaint, 99, reinterpret_cast<DWORD_PTR>(&immediatePaints));
        for (UINT dpi : {96u, 144u, 192u}) for (int count : {0, 2, 5}) {
            window.dpi_ = dpi;
            window.snapshot_.routes.clear();
            for (int i = 0; i < count; ++i)
                window.snapshot_.routes.push_back({{"test-" + std::to_string(i), "Bluetooth headphones " + std::to_string(i + 1), 35 + i * 10, true}, RouteState::Playing, {}});
            window.fonts(); window.expanded_ = true; window.update();
            for (int height : {480, 640}) {
                SetWindowPos(handle, nullptr, 0, 0,
                    std::min(window.d(660), static_cast<int>(work.right - work.left - 20)),
                    std::min(window.d(height), static_cast<int>(work.bottom - work.top - 20)),
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                for (int offset : {0, 60, 120, 340, 2000, 1000, 200, 0}) {
                    window.scroll(window.d(offset));
                    verify(window, "immediate scroll frame: dpi=" + std::to_string(dpi) + ", outputs=" + std::to_string(count) + ", offset=" + std::to_string(offset), false); ++checks;
                }
                // Expand/collapse, timer refresh and keyboard focus also reposition controls.
                window.expanded_ = false; window.update(); verify(window, "collapse"); ++checks;
                window.expanded_ = true; window.update(); verify(window, "expand"); ++checks;
                SendMessageW(handle, controlFocused, reinterpret_cast<WPARAM>(window.buttons_[aboutId]), 0);
                verify(window, "focus scroll"); ++checks;
                SendMessageW(handle, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
                verify(window, "mouse wheel"); ++checks;
                window.update(); verify(window, "timer refresh"); ++checks;
                window.snapshot_.settings.muted = true; window.update(); verify(window, "master mute"); ++checks;
                window.snapshot_.settings.muted = false; window.update(); verify(window, "master unmute"); ++checks;
                if (!window.rows_.empty()) {
                    window.scroll(window.d(outputTop - 8));
                    verify(window, "prepare idle frame"); ++checks;
                    // Off-screen children may retain dirty regions from earlier
                    // layouts; isolate invalidations caused by this refresh.
                    for (HWND child = GetWindow(handle, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) ValidateRect(child, nullptr);
                    immediatePaints = 0;
                    window.update();
                    if (immediatePaints) throw std::runtime_error("Idle update synchronously repaints " + std::to_string(immediatePaints) + " buttons.");
                    for (HWND control = GetWindow(handle, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT))
                        if (!isSpectrum(control) && GetUpdateRect(control, nullptr, FALSE)) throw std::runtime_error("Idle refresh invalidates control " + std::to_string(GetDlgCtrlID(control)));
                    verify(window, "idle refresh keeps controls"); ++checks;
                    for (HWND child = GetWindow(handle, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) ValidateRect(child, nullptr);
                    ValidateRect(handle, nullptr);
                    window.snapshot_.spectrum.fill(.6f); window.update();
                    if (GetUpdateRect(handle, nullptr, FALSE)) throw std::runtime_error("Spectrum refresh invalidates the main surface.");
                    for (HWND control = GetWindow(handle, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT))
                        if (!isSpectrum(control) && GetUpdateRect(control, nullptr, FALSE)) throw std::runtime_error("Spectrum refresh invalidates control " + std::to_string(GetDlgCtrlID(control)));
                    verify(window, "meter refresh keeps controls"); ++checks;
                    int savedVolume = window.snapshot_.routes[0].preference.volume;
                    SendMessageW(window.rows_[0].mute, BM_CLICK, 0, 0);
                    if (!window.snapshot_.routes[0].preference.muted) throw std::runtime_error("Device mute click failed.");
                    verify(window, "device mute click"); ++checks;
                    window.snapshot_.settings.muted = true; window.update();
                    verify(window, "master plus device mute"); ++checks;
                    SendMessageW(window.rows_[0].mute, BM_CLICK, 0, 0);
                    if (window.snapshot_.routes[0].preference.muted || !window.snapshot_.settings.muted || window.snapshot_.routes[0].preference.volume != savedVolume)
                        throw std::runtime_error("Device unmute changed slider or master mute.");
                    verify(window, "device unmute under master mute"); ++checks;
                    window.snapshot_.settings.muted = false; window.update();
                    verify(window, "device and master restored"); ++checks;
                    HWND slider = window.rows_[0].slider;
                    SetFocus(slider);
                    RECT thumb{}, bounds{};
                    SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
                    GetClientRect(slider, &bounds);
                    SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((thumb.left + thumb.right) / 2, (thumb.top + thumb.bottom) / 2));
                    for (int step : {1, 3, 6, 9, 7, 4, 2, 0}) {
                        SendMessageW(slider, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(8 + (bounds.right - 16) * step / 10, (thumb.top + thumb.bottom) / 2));
                        verify(window, "drag before timer refresh"); ++checks;
                        for (std::size_t i = 0; i < spectrumBands; ++i) window.snapshot_.spectrum[i] = .4f + .4f * std::sin(static_cast<float>(i + step));
                        SendMessageW(handle, WM_TIMER, 1, 0);
                        verify(window, "drag during timer refresh"); ++checks;
                    }
                    SendMessageW(slider, WM_LBUTTONUP, 0, MAKELPARAM(8, (thumb.top + thumb.bottom) / 2));
                    window.scroll(window.d(340)); verify(window, "scroll after drag"); ++checks;
                    if (count == 5) {
                        double totalMs = 0;
                        for (int step = 0; step < 12; ++step) {
                            window.snapshot_.spectrum.fill(.25f + .05f * step);
                            SendMessageW(handle, WM_TIMER, 1, 0);
                            auto start = std::chrono::steady_clock::now();
                            SendMessageW(handle, WM_MOUSEWHEEL, MAKEWPARAM(0, step < 6 ? -WHEEL_DELTA : WHEEL_DELTA), 0);
                            totalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                            verify(window, "wheel burst with live spectra", false); ++checks;
                        }
                        std::cout << "Scroll frame mean (5 outputs, " << dpi << " DPI, " << height << " height): " << totalMs / 12 << " ms\n";
                    }
                }
            }
        }
        DestroyWindow(handle);
        return checks;
    }
};
}
}

int main(int argc, char** argv) {
    using namespace splitter;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        appInstance = GetModuleHandleW(nullptr);
        if (!FindResourceW(appInstance, MAKEINTRESOURCEW(1), RT_MANIFEST)) throw std::runtime_error("UI tests require the same manifest as the app.");
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
        Gdiplus::GdiplusStartupInput startup; ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) throw std::runtime_error("GDI+ initialization failed.");
        struct Graphics { ULONG_PTR token; ~Graphics() { Gdiplus::GdiplusShutdown(token); } } graphics{token};
        WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = appInstance;
        cls.lpfnWndProc = Window::proc; cls.lpszClassName = L"BtAudioSplitterNative";
        if (!RegisterClassExW(&cls)) throw std::runtime_error("Test window registration failed.");
        cls.lpfnWndProc = Window::spectrumProc; cls.lpszClassName = L"BtAudioSplitterSpectrum";
        if (!RegisterClassExW(&cls)) throw std::runtime_error("Test spectrum registration failed.");
        bool liveOnly = argc == 2 && std::string(argv[1]) == "--live-only";
        if (argc > 1 && !liveOnly) throw std::runtime_error("Usage: ui-tests.exe [--live-only]");
        auto checks = WindowRenderingTest::run(liveOnly);
        if (liveOnly) return 0;
        std::cout << "PASS " << checks << " UI repaint comparisons (themed controls; 0/2/5 outputs; 100/150/200% scale; dragging, meters, mute, scroll, resize, focus).\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
